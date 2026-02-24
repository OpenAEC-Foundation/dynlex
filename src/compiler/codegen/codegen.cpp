#include "codegen.h"
#include "classDefinition.h"
#include "classSection.h"
#include "codegenInternal.h"
#include "compiler.h"
#include "compilerUtils.h"
#include "expression.h"
#include "intrinsicInfo.h"
#include "native.h"
#include "patternDefinition.h"
#include "patternReference.h"
#include "spirv.h"
#include "type.h"
#include "variable.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include <algorithm>
#include <unordered_map>

// Generate a monomorphized LLVM function for a pattern definition with specific argument types.
// The Instantiation's llvmFunction is set before generating the body, enabling recursive calls.
void generateSpecializedFunction(
	ParseContext &context, Section *section, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::vector<DataType> &argTypes, Instantiation &inst
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	std::vector<std::string> varNames;
	for (const auto &[name, expr] : paramBindings) {
		varNames.push_back(name);
	}

	// All parameters are opaque pointers
	std::vector<llvm::Type *> paramTypes(varNames.size(), llvm::PointerType::getUnqual(*context.llvmContext));

	auto it = section->instantiations.find(argTypes);
	assert(it != section->instantiations.end() && "Missing instantiation for arg types");
	if (!it->second.returnType.isDeduced()) {
		fprintf(
			stderr, "UNDEDUCED: '%s' args=[",
			section->patternDefinitions.empty() ? "?" : std::string(section->patternDefinitions[0]->range.subString).c_str()
		);
		for (auto &t : argTypes)
			fprintf(stderr, "%s ", t.toString().c_str());
		fprintf(stderr, "]\n");
		fflush(stderr);
		assert(false && "Return type must be deduced before codegen");
	}
	llvm::Type *returnType = getLLVMType(context, it->second.returnType);

	llvm::FunctionType *funcType = llvm::FunctionType::get(returnType, paramTypes, false);

	// Name includes type signature for uniqueness
	std::string funcName = getPatternFunctionName(section);
	for (const DataType &t : argTypes) {
		funcName += "_" + t.toString();
	}

	llvm::Function *func = llvm::Function::Create(funcType, llvm::Function::InternalLinkage, funcName, context.llvmModule);
	inst.llvmFunction = func;

	size_t argIdx = 0;
	for (auto &arg : func->args()) {
		arg.setName(varNames[argIdx++]);
	}

	// Create debug info subprogram
	llvm::DIScope *savedDebugScope = context.currentDebugScope;
	if (context.diBuilder && !section->codeLines.empty()) {
		CodeLine *firstLine = section->codeLines[0];
		llvm::DIFile *diFile = getOrCreateDIFile(context, firstLine->sourceFile);
		unsigned line = firstLine->sourceFileLineIndex + 1;
		auto *funcDIType = context.diBuilder->createSubroutineType(context.diBuilder->getOrCreateTypeArray(std::nullopt));
		auto *sp = context.diBuilder->createFunction(
			diFile, funcName, funcName, diFile, line, funcDIType, line, llvm::DINode::FlagPrototyped,
			llvm::DISubprogram::SPFlagDefinition
		);
		func->setSubprogram(sp);
		context.currentDebugScope = sp;
	}

	llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context.llvmContext, "entry", func);

	// Save all codegen state
	llvm::BasicBlock *savedBlock = builder.GetInsertBlock();
	llvm::BasicBlock::iterator savedPoint = builder.GetInsertPoint();
	llvm::DebugLoc savedDebugLoc = builder.getCurrentDebugLocation();
	auto savedPatternBindings = context.patternBindings;
	auto savedParamTypes = context.patternParamTypes;
	// Push macro bindings — function bodies must not see caller's macro bindings
	context.macroBindingStack.push(context.macroExpressionBindings);
	context.macroExpressionBindings.clear();

	builder.SetInsertPoint(entry);

	// Set up bindings: map parameter names to LLVM values and their types
	context.patternBindings.clear();
	context.patternParamTypes.clear();
	argIdx = 0;
	for (auto &arg : func->args()) {
		context.patternBindings[varNames[argIdx]] = &arg;
		context.patternParamTypes[varNames[argIdx]] = argTypes[argIdx];
		argIdx++;
	}

	// Generate function body
	for (Section *child : section->children) {
		generateSectionCode(context, child);
	}

	// Add implicit void return if the function returns void
	if (inst.returnType.kind == DataType::Kind::Void) {
		builder.CreateRetVoid();
	}

	// Restore all codegen state
	context.macroExpressionBindings = context.macroBindingStack.top();
	context.macroBindingStack.pop();
	context.patternBindings = savedPatternBindings;
	context.patternParamTypes = savedParamTypes;
	context.currentDebugScope = savedDebugScope;

	if (savedBlock) {
		builder.SetInsertPoint(savedBlock, savedPoint);
		builder.SetCurrentDebugLocation(savedDebugLoc);
	}
}

// Generate code for an expression
llvm::Value *generateExpressionCode(ParseContext &context, Expression *expr) {
	if (!expr)
		return nullptr;

	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	switch (expr->kind) {
	case Expression::Kind::Literal: {
		if (auto *doubleVal = std::get_if<double>(&expr->literalValue)) {
			DataType numType = getEffectiveType(context, expr);
			llvm::Type *llvmType = numType.toLLVM(*context.llvmContext);
			if (numType.kind == DataType::Kind::Int)
				return llvm::ConstantInt::get(llvmType, (int64_t)*doubleVal, true);
			return llvm::ConstantFP::get(llvmType, *doubleVal);
		}
		if (auto *strVal = std::get_if<std::string>(&expr->literalValue)) {
			// TODO: strings are currently just i8* pointers to constant data.
			// String operations (concatenation, slicing, etc.) need runtime support.
			auto it = context.stringConstants.find(*strVal);
			if (it != context.stringConstants.end())
				return it->second;
			std::string globalName = ".str." + std::to_string(context.stringConstants.size());
			llvm::Constant *strConst = llvm::ConstantDataArray::getString(*context.llvmContext, *strVal, true);
			llvm::GlobalVariable *strGlobal = new llvm::GlobalVariable(
				*context.llvmModule, strConst->getType(), true, llvm::GlobalValue::PrivateLinkage, strConst, globalName
			);
			context.stringConstants[*strVal] = strGlobal;
			return strGlobal;
		}
		// Unknown literal variant type - should never reach here after type inference
		ASSERT_UNREACHABLE("Unknown literal type in codegen");
	}

	case Expression::Kind::Variable: {
		Expression *resolved = resolveVariableBinding(context, expr);
		if (resolved != expr) {
			MacroScopeGuard guard(context);
			if (!context.macroBindingStack.empty())
				guard.popToCallerScope();
			return generateExpressionCode(context, resolved);
		}

		if (!expr->variable)
			return nullptr;
		std::string varName = expr->variable->name;

		// Determine this variable's type for loading
		DataType varType = getEffectiveType(context, expr);

		// Class types: return the pointer directly (structs are passed by pointer)
		if (varType.kind == DataType::Kind::Class) {
			auto bindingIt = context.patternBindings.find(varName);
			if (bindingIt != context.patternBindings.end())
				return bindingIt->second;
			VariableReference *varRef = expr->variable;
			VariableReference *definition = varRef->definition ? varRef->definition : varRef;
			if (definition->alloca)
				return definition->alloca;
		}

		llvm::Type *loadType = getLLVMType(context, varType);

		// Pattern parameter: load from function argument pointer
		auto bindingIt = context.patternBindings.find(varName);
		if (bindingIt != context.patternBindings.end())
			return builder.CreateAlignedLoad(loadType, bindingIt->second, llvm::Align(8), varName + "_val");

		// Local variable: load from alloca
		VariableReference *varRef = expr->variable;
		VariableReference *definition = varRef->definition ? varRef->definition : varRef;
		if (definition->alloca)
			return builder.CreateAlignedLoad(loadType, definition->alloca, llvm::Align(8), varName + "_val");

		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Unknown variable: " + varName, expr->range));
		return nullptr;
	}

	case Expression::Kind::PatternCall: {
		if (!expr->patternMatch || !expr->patternMatch->matchedEndNode)
			return nullptr;

		auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
		if (defs.empty())
			return nullptr;

		// Sort arguments by source position (expandMatch appends submatches/variables/words
		// after direct args, so they may not be in text order)
		std::vector<Expression *> sortedArgs = sortArgumentsByPosition(expr->arguments);

		// Build argument types for overload selection
		std::vector<DataType> argTypesForOverload;
		{
			size_t ai = 0;
			for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
				bool isParam = false;
				for (auto *d : defs) {
					if (node->parameterNames.contains(d)) {
						isParam = true;
						break;
					}
				}
				if (isParam && ai < sortedArgs.size()) {
					argTypesForOverload.push_back(getEffectiveType(context, sortedArgs[ai]));
					ai++;
				}
			}
		}

		// Select the best overload based on argument types
		PatternDefinition *matchedDef = selectOverload(defs, sortedArgs, expr->patternMatch->nodesPassed, argTypesForOverload);
		if (!matchedDef)
			matchedDef = defs[0];

		Section *matchedSection = matchedDef->section;
		if (!matchedSection)
			return nullptr;

		// Non-macro class type references are compile-time only — no runtime code.
		// Macro class sections (primitive type definitions) fall through to macro expansion.
		if (matchedSection->type == SectionType::Class && !matchedSection->isMacro)
			return nullptr;

		// Build parameter name → argument expression mapping
		std::vector<std::pair<std::string, Expression *>> paramBindings;
		size_t argIndex = 0;
		for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
			auto paramIt = node->parameterNames.find(matchedDef);
			if (paramIt != node->parameterNames.end() && argIndex < sortedArgs.size()) {
				paramBindings.push_back({paramIt->second, sortedArgs[argIndex++]});
			}
		}
		if (matchedSection->isMacro) {
			// Macro: inline the body with expression substitution.
			// Push current bindings and set only this macro's parameters (scoped).
			context.macroBindingStack.push(context.macroExpressionBindings);
			context.macroExpressionBindings.clear();
			Section *savedBodySection = context.currentBodySection;

			for (const auto &[paramName, argExpr] : paramBindings) {
				context.macroExpressionBindings[paramName] = argExpr;
			}

			// Only section-type macros (like "if condition:", "loop while condition:")
			// should pick up and process the body section opened by this line.
			// Expression/effect macros (like "not value:", "a + b") must NOT process
			// the body section, even if they appear on a line that opens one.
			Section *bodySection = nullptr;
			if (matchedSection->type == SectionType::Section) {
				bodySection = expr->range.line ? expr->range.line->sectionOpening : nullptr;
				context.currentBodySection = bodySection;
			}

			llvm::Value *result = nullptr;
			for (Section *child : matchedSection->children) {
				for (CodeLine *line : child->codeLines) {
					if (line->expression)
						result = generateExpressionCode(context, line->expression);
				}
			}

			if (bodySection) {
				generateSectionCode(context, bodySection);
				if (bodySection->exitBlock) {
					if (!builder.GetInsertBlock()->getTerminator()) {
						llvm::BasicBlock *target =
							bodySection->branchBackBlock ? bodySection->branchBackBlock : bodySection->exitBlock;
						builder.CreateBr(target);
					}
					builder.SetInsertPoint(bodySection->exitBlock);
				}
			}

			context.macroExpressionBindings = context.macroBindingStack.top();
			context.macroBindingStack.pop();
			context.currentBodySection = savedBodySection;
			return result;
		}

		// Non-macro pattern: monomorphized function call.
		// Compute argument types at this call site for specialization.
		std::vector<DataType> argTypes;
		for (const auto &[paramName, argExpr] : paramBindings) {
			DataType t = getEffectiveType(context, argExpr);
			assert(t.isDeduced() && "Undeduced argument type at codegen");
			argTypes.push_back(t);
		}

		// Look up or generate the specialized function
		Instantiation &inst = matchedSection->instantiations[argTypes];
		if (!inst.llvmFunction) {
			generateSpecializedFunction(context, matchedSection, paramBindings, argTypes, inst);
		}
		llvm::Function *func = inst.llvmFunction;

		// Build call arguments: pass variable pointers or temp allocas
		std::vector<llvm::Value *> args;
		for (size_t i = 0; i < paramBindings.size(); i++) {
			Expression *argExpr = paramBindings[i].second;
			llvm::Value *ptr = getVariablePointer(context, argExpr);
			if (ptr) {
				args.push_back(ptr);
			} else {
				llvm::Value *argVal = generateExpressionCode(context, argExpr);
				if (argVal) {
					llvm::AllocaInst *tempAlloca = createEntryAlloca(context, "tmp", argTypes[i]);
					builder.CreateAlignedStore(argVal, tempAlloca, llvm::Align(8));
					args.push_back(tempAlloca);
				}
			}
		}

		return builder.CreateCall(func, args);
	}

	case Expression::Kind::IntrinsicCall: {
		std::vector<Expression *> args(expr->arguments.begin() + 1, expr->arguments.end());
		return generateIntrinsicCode(context, expr->intrinsicName, args, getEffectiveType(context, expr));
	}

	case Expression::Kind::Pending:
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Unresolved pending expression", expr->range));
		return nullptr;
	}

	return nullptr;
}

// Generate code for a section (process pattern references)
bool generateSectionCode(ParseContext &context, Section *section) {
	allocateSectionVariables(context, section);

	for (CodeLine *line : section->codeLines) {
		if (line->expression) {
			if (context.diBuilder && line->sourceFile && context.currentDebugScope) {
				auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
				// Use a DILexicalBlockFile scope when the code line's source file
				// differs from the current scope's file (e.g., imported code in main)
				llvm::DIScope *scope = context.currentDebugScope;
				llvm::DIFile *lineFile = getOrCreateDIFile(context, line->sourceFile);
				if (lineFile && lineFile != scope->getFile())
					scope = context.diBuilder->createLexicalBlockFile(scope, lineFile);
				builder.SetCurrentDebugLocation(
					llvm::DILocation::get(*context.llvmContext, line->sourceFileLineIndex + 1, 0, scope)
				);
			}
			generateExpressionCode(context, line->expression);
		}
	}

	return true;
}

bool generateCode(ParseContext &context) {
	context.llvmContext = new llvm::LLVMContext();
	context.llvmModule = new llvm::Module("dynlex_module", *context.llvmContext);
	context.llvmBuilder = new llvm::IRBuilder<>(*context.llvmContext);
	if (context.options.emitSPIRV) {
		context.llvmModule->setTargetTriple("spirv-unknown-vulkan1.3");
	} else {
		context.llvmModule->setTargetTriple(llvm::sys::getDefaultTargetTriple());
	}

	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	// Initialize debug info builder (skip for SPIR-V — no DWARF in SPIR-V)
	if (context.options.emitDebugInfo && !context.options.emitSPIRV) {
		context.diBuilder = new llvm::DIBuilder(*context.llvmModule);
		llvm::DIFile *mainFile = getOrCreateDIFile(context, context.mainSourceFile);
		context.diCompileUnit = context.diBuilder->createCompileUnit(
			llvm::dwarf::DW_LANG_C, mainFile, "DynLex Compiler", context.options.optimizationLevel > 0, "", 0
		);
		context.currentDebugScope = context.diCompileUnit;

		context.llvmModule->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 5);
		context.llvmModule->addModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);
	}

	// No first pass — non-macro functions are generated on-demand via monomorphization.

	// In SPIR-V mode, declare shader I/O globals before generating code
	llvm::GlobalVariable *shaderInputGlobal = nullptr;
	llvm::GlobalVariable *shaderOutputGlobal = nullptr;
	if (context.options.emitSPIRV) {
		llvm::Type *vec4Ty = llvm::FixedVectorType::get(builder.getFloatTy(), 4);
		bool isVertex = context.options.shaderStage == ParseContext::ShaderStage::Vertex;

		// Input global (address space 1 = SPIR-V Input storage class)
		std::string inputName = isVertex ? "in_Position" : "gl_FragCoord";
		shaderInputGlobal = new llvm::GlobalVariable(
			*context.llvmModule, vec4Ty, false, llvm::GlobalValue::ExternalLinkage, nullptr, inputName, nullptr,
			llvm::GlobalValue::NotThreadLocal, 1
		);
		shaderInputGlobal->setInitializer(llvm::Constant::getNullValue(vec4Ty));

		// Output global (address space 2 = SPIR-V Output storage class)
		std::string outputName = isVertex ? "gl_Position" : "gl_FragColor";
		shaderOutputGlobal = new llvm::GlobalVariable(
			*context.llvmModule, vec4Ty, false, llvm::GlobalValue::ExternalLinkage, nullptr, outputName, nullptr,
			llvm::GlobalValue::NotThreadLocal, 2
		);
		shaderOutputGlobal->setInitializer(llvm::Constant::getNullValue(vec4Ty));

		// Shader uniform globals are created lazily during codegen when
		// "shader uniform" intrinsics are encountered (see generateIntrinsicCode).
	}

	// Create main function: void main() for shaders, int main() for native
	llvm::Function *mainFunc;
	if (context.options.emitSPIRV) {
		llvm::FunctionType *mainType = llvm::FunctionType::get(builder.getVoidTy(), false);
		mainFunc = llvm::Function::Create(mainType, llvm::Function::ExternalLinkage, "main", context.llvmModule);
	} else {
		llvm::FunctionType *mainType = llvm::FunctionType::get(builder.getInt32Ty(), false);
		mainFunc = llvm::Function::Create(mainType, llvm::Function::ExternalLinkage, "main", context.llvmModule);
	}

	// Create debug info subprogram for main
	if (context.diBuilder) {
		llvm::DIFile *mainFile = getOrCreateDIFile(context, context.mainSourceFile);
		unsigned mainLine = 1;
		auto *mainFuncDIType = context.diBuilder->createSubroutineType(context.diBuilder->getOrCreateTypeArray(std::nullopt));
		auto *mainSP = context.diBuilder->createFunction(
			mainFile, "main", "main", mainFile, mainLine, mainFuncDIType, mainLine, llvm::DINode::FlagPrototyped,
			llvm::DISubprogram::SPFlagDefinition
		);
		mainFunc->setSubprogram(mainSP);
		context.currentDebugScope = mainSP;
	}

	llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context.llvmContext, "entry", mainFunc);
	builder.SetInsertPoint(entry);

	if (!generateSectionCode(context, context.mainSection))
		return false;

	if (context.options.emitSPIRV) {
		builder.CreateRetVoid();
	} else {
		builder.CreateRet(builder.getInt32(0));
	}

	// Add SPIR-V metadata for shader execution model and decorations
	if (context.options.emitSPIRV) {
		llvm::LLVMContext &ctx = *context.llvmContext;
		bool isVertex = context.options.shaderStage == ParseContext::ShaderStage::Vertex;

		// spirv.ExecutionMode: OriginUpperLeft (required for Fragment only)
		if (!isVertex) {
			llvm::Metadata *execModeOps[] = {
				llvm::ValueAsMetadata::get(mainFunc),
				llvm::ConstantAsMetadata::get(builder.getInt32(7)), // OriginUpperLeft
			};
			llvm::MDNode *execModeNode = llvm::MDNode::get(ctx, execModeOps);
			context.llvmModule->getOrInsertNamedMetadata("spirv.ExecutionMode")->addOperand(execModeNode);
		}
	}

	// Finalize debug info before verification
	if (context.diBuilder)
		context.diBuilder->finalize();

	// Verify
	std::string error;
	llvm::raw_string_ostream errorStream(error);
	if (llvm::verifyModule(*context.llvmModule, &errorStream)) {
		llvm::errs() << "\n=== Invalid LLVM IR (for debugging) ===\n";
		context.llvmModule->print(llvm::errs(), nullptr);
		llvm::errs() << "=== End Invalid LLVM IR ===\n\n";
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "LLVM verification failed: " + error, Range()));
		return false;
	}

	// Optimization passes
	if (context.options.optimizationLevel > 0) {
		llvm::LoopAnalysisManager lam;
		llvm::FunctionAnalysisManager fam;
		llvm::CGSCCAnalysisManager cgam;
		llvm::ModuleAnalysisManager mam;

		llvm::PassBuilder pb;
		pb.registerModuleAnalyses(mam);
		pb.registerCGSCCAnalyses(cgam);
		pb.registerFunctionAnalyses(fam);
		pb.registerLoopAnalyses(lam);
		pb.crossRegisterProxies(lam, fam, cgam, mam);

		llvm::OptimizationLevel optLevel;
		switch (context.options.optimizationLevel) {
		case 1:
			optLevel = llvm::OptimizationLevel::O1;
			break;
		case 2:
			optLevel = llvm::OptimizationLevel::O2;
			break;
		case 3:
			optLevel = llvm::OptimizationLevel::O3;
			break;
		default:
			optLevel = llvm::OptimizationLevel::O1;
			break;
		}

		llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(optLevel);
		mpm.run(*context.llvmModule, mam);
	}

	// Output
	if (context.options.emitSPIRV) {
		if (!emitSPIRVModule(context))
			return false;
	} else if (context.options.emitLLVM) {
		std::string outputPath = context.options.outputPath;
		if (outputPath.empty())
			outputPath = context.options.inputPath + ".ll";
		std::error_code ec;
		llvm::raw_fd_ostream out(outputPath, ec);
		if (ec) {
			context.diagnostics.push_back(
				Diagnostic(Diagnostic::Level::Error, "Failed to open output file: " + ec.message(), Range())
			);
			return false;
		}
		context.llvmModule->print(out, nullptr);
	} else {
		if (!emitNativeExecutable(context))
			return false;
	}

	return true;
}
