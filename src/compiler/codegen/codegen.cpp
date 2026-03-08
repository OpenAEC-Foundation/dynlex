#include "codegen.h"
#include "classDefinition.h"
#include "classSection.h"
#include "codegenInternal.h"
#include "compiler.h"
#include "compilerUtils.h"
#include "compileTimeValue.h"
#include "function.h"
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
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include <algorithm>
#include <unordered_map>

// Generate a monomorphized LLVM function for a pattern definition with specific argument types.
// The Instantiation's llvmFunction is set before generating the body, enabling recursive calls.
void generateSpecializedFunction(
	ParseContext &context, Section *section, const std::vector<std::pair<std::string, Function *>> &paramBindings,
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
		std::unordered_map<std::string, Function *> callBindings;
		for (const auto &[name, expr] : paramBindings)
			callBindings[name] = expr;
		ensureSectionInstantiationInferred(context, section, callBindings, argTypes);
		it = section->instantiations.find(argTypes);
	}
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
	const Instantiation *savedCodegenInstantiation = context.currentCodegenInstantiation;
	// Push macro bindings — function bodies must not see caller's macro bindings
	context.macroBindingStack.push(context.macroFunctionBindings);
	context.macroFunctionBindings.clear();

	builder.SetInsertPoint(entry);

	// Set up bindings: map parameter names to LLVM values and their types
	context.patternBindings.clear();
	context.patternParamTypes.clear();
	const std::vector<DataType> &parameterTypes =
		inst.parameterTypes.size() == argTypes.size() ? inst.parameterTypes : argTypes;
	argIdx = 0;
	for (auto &arg : func->args()) {
		context.patternBindings[varNames[argIdx]] = &arg;
		context.patternParamTypes[varNames[argIdx]] = parameterTypes[argIdx];
		argIdx++;
	}
	context.currentCodegenInstantiation = &inst;

	// Generate function body
	for (Section *child : section->children) {
		generateSectionCode(context, child);
	}

	// Add implicit void return if the function returns void
	if (inst.returnType.kind == DataType::Kind::Void) {
		builder.CreateRetVoid();
	}

	// Restore all codegen state
	context.macroFunctionBindings = context.macroBindingStack.top();
	context.macroBindingStack.pop();
	context.patternBindings = savedPatternBindings;
	context.patternParamTypes = savedParamTypes;
	context.currentCodegenInstantiation = savedCodegenInstantiation;
	context.currentDebugScope = savedDebugScope;

	if (savedBlock) {
		builder.SetInsertPoint(savedBlock, savedPoint);
		builder.SetCurrentDebugLocation(savedDebugLoc);
	}
}

static bool expandsToSelectIntrinsic(Function *function) {
	std::unordered_map<std::string, Function *> ignoredBindings;
	Function *bodyExpr = expandMacroPatternCall(function, ignoredBindings);
	return bodyExpr && bodyExpr->kind == Function::Kind::IntrinsicCall &&
		   intrinsicKind(bodyExpr->intrinsicName) == IntrinsicKind::Select;
}

// Generate code for an function
llvm::Value *generateFunctionCode(ParseContext &context, Function *expr) {
	if (!expr)
		return nullptr;

	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	switch (expr->kind) {
	case Function::Kind::Literal: {
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
			if (it != context.stringConstants.end()) {
				llvm::GlobalVariable *strGlobal = it->second;
				return builder.CreateInBoundsGEP(
					strGlobal->getValueType(), strGlobal, {builder.getInt64(0), builder.getInt64(0)}, "str_ptr"
				);
			}
			std::string globalName = ".str." + std::to_string(context.stringConstants.size());
			llvm::Constant *strConst = llvm::ConstantDataArray::getString(*context.llvmContext, *strVal, true);
			llvm::GlobalVariable *strGlobal = new llvm::GlobalVariable(
				*context.llvmModule, strConst->getType(), true, llvm::GlobalValue::PrivateLinkage, strConst, globalName
			);
			context.stringConstants[*strVal] = strGlobal;
			return builder.CreateInBoundsGEP(
				strGlobal->getValueType(), strGlobal, {builder.getInt64(0), builder.getInt64(0)}, "str_ptr"
			);
			}
			// Unknown literal variant type - should never reach here after type inference
			crashCompilerBug("Unknown literal type in codegen");
		}

	case Function::Kind::ArrayLiteral: {
		DataType arrayType = getEffectiveType(context, expr);
		if (arrayType.kind != DataType::Kind::Array || !arrayType.arrayElementType)
			return nullptr;
		llvm::Type *llvmArrayType = getLLVMType(context, arrayType);
		llvm::AllocaInst *tempAlloca = createEntryAlloca(context, "array_literal", arrayType);
		for (size_t i = 0; i < expr->arguments.size(); i++) {
			llvm::Value *elementValue = generateFunctionCode(context, expr->arguments[i]);
			DataType fromType = getEffectiveType(context, expr->arguments[i]);
			elementValue = ensureType(context, elementValue, fromType, *arrayType.arrayElementType);
			llvm::Value *elementPtr = builder.CreateGEP(
				llvmArrayType, tempAlloca, {builder.getInt64(0), builder.getInt64(static_cast<int64_t>(i))}, "array_elem_ptr"
			);
			builder.CreateStore(elementValue, elementPtr);
		}
		return builder.CreateAlignedLoad(llvmArrayType, tempAlloca, llvm::Align(8), "array_literal_load");
	}

	case Function::Kind::Variable: {
		Function *resolved = resolveVariableBinding(context, expr);
		if (resolved != expr) {
			MacroScopeGuard guard(context);
			if (!context.macroBindingStack.empty())
				guard.popToCallerScope();
			return generateFunctionCode(context, resolved);
		}

		if (!expr->variable)
			return nullptr;
		std::string varName = expr->variable->name;

		// Determine this variable's type for loading
		DataType varType = getEffectiveType(context, expr);

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

	case Function::Kind::PatternCall: {
		if (!expr->patternMatch || !expr->patternMatch->matchedEndNode)
			return nullptr;

		auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
		if (defs.empty())
			return nullptr;

		// Arguments are already sorted by source position (type inference sorts in-place)

		// Build argument types for overload selection
		std::vector<DataType> argTypesForOverload;
		if (!expandsToSelectIntrinsic(expr)) {
			size_t ai = 0;
			for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
				bool isParam = false;
				for (auto *d : defs) {
					if (node->parameterNames.contains(d)) {
						isParam = true;
						break;
					}
				}
				if (isParam && ai < expr->arguments.size()) {
					argTypesForOverload.push_back(getEffectiveType(context, expr->arguments[ai]));
					ai++;
				}
			}
		}

		// Select the best overload based on argument types
		PatternDefinition *matchedDef =
			selectOverload(defs, expr->arguments, expr->patternMatch->nodesPassed, argTypesForOverload);
		if (!matchedDef) {
			context.diagnostics.push_back(
				Diagnostic(Diagnostic::Level::Error, "No overload matches argument types", expr->range)
			);
			return nullptr;
		}

		Section *matchedSection = matchedDef->section;
		if (!matchedSection)
			return nullptr;

		// Non-macro class type references are compile-time only — no runtime code.
		// Macro class sections (primitive type definitions) fall through to macro expansion.
		if (matchedSection->type == SectionType::Class && !matchedSection->isMacro)
			return nullptr;

		// Build parameter name → argument function mapping
		std::vector<std::pair<std::string, Function *>> paramBindings;
		size_t argIndex = 0;
		for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
			auto paramIt = node->parameterNames.find(matchedDef);
			if (paramIt != node->parameterNames.end() && argIndex < expr->arguments.size()) {
				paramBindings.push_back({paramIt->second, expr->arguments[argIndex++]});
			}
		}
		if (matchedSection->isMacro) {
			// Macro: inline the body with function substitution.
			// Push current bindings and set only this macro's parameters (scoped).
			context.macroBindingStack.push(context.macroFunctionBindings);
			context.macroFunctionBindings.clear();
			Section *savedBodySection = context.currentBodySection;

			for (const auto &[paramName, argExpr] : paramBindings) {
				context.macroFunctionBindings[paramName] = argExpr;
			}

			// Only section-type macros (like "if condition:", "loop while condition:")
			// should pick up and process the body section opened by this line.
			// Function macros (like "not value:", "a + b") must NOT process
			// the body section, even if they appear on a line that opens one.
			Section *bodySection = nullptr;
			if (matchedSection->type == SectionType::Section) {
				bodySection = expr->range.line ? expr->range.line->sectionOpening : nullptr;
				context.currentBodySection = bodySection;
			}

			llvm::Value *result = nullptr;
			for (Section *child : matchedSection->children) {
				for (CodeLine *line : child->codeLines) {
					if (line->function)
						result = generateFunctionCode(context, line->function);
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

			context.macroFunctionBindings = context.macroBindingStack.top();
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
			Function *argExpr = paramBindings[i].second;
			llvm::Value *ptr = getVariablePointer(context, argExpr);
			if (ptr) {
				args.push_back(ptr);
			} else {
				llvm::Value *argVal = generateFunctionCode(context, argExpr);
				if (argVal) {
					llvm::AllocaInst *tempAlloca = createEntryAlloca(context, "tmp", argTypes[i]);
					builder.CreateAlignedStore(argVal, tempAlloca, llvm::Align(8));
					args.push_back(tempAlloca);
				}
			}
		}

		return builder.CreateCall(func, args);
	}

	case Function::Kind::IntrinsicCall: {
		std::vector<Function *> args(expr->arguments.begin() + 1, expr->arguments.end());
		return generateIntrinsicCode(context, expr->intrinsicName, args, getEffectiveType(context, expr));
	}

	case Function::Kind::Pending:
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Unresolved pending function", expr->range));
		return nullptr;
	}

	return nullptr;
}

// Generate code for a section (process pattern references)
bool generateSectionCode(ParseContext &context, Section *section) {
	allocateSectionVariables(context, section);

	auto controlHeaderInfo = [&](CodeLine *line)
		-> std::optional<std::tuple<std::string, Function *, std::unordered_map<std::string, Function *>>> {
		if (!line || !line->function)
			return std::nullopt;

		Function *header = line->function;
		std::unordered_map<std::string, Function *> headerBindings = context.macroFunctionBindings;
		if (header->kind == Function::Kind::PatternCall) {
			std::unordered_map<std::string, Function *> innerBindings;
			Function *expanded = expandMacroPatternCall(header, innerBindings);
			if (expanded) {
				header = expanded;
				for (const auto &[name, argExpr] : innerBindings)
					headerBindings[name] = argExpr;
			}
		}
		if (!header || header->kind != Function::Kind::IntrinsicCall)
			return std::nullopt;
		if (header->intrinsicName != "if" && header->intrinsicName != "else if" && header->intrinsicName != "else")
			return std::nullopt;
		return std::make_optional(std::make_tuple(header->intrinsicName, header, std::move(headerBindings)));
	};

	for (size_t i = 0; i < section->codeLines.size(); i++) {
		CodeLine *line = section->codeLines[i];
		auto headerInfo = controlHeaderInfo(line);
		if (headerInfo && std::get<0>(*headerInfo) == "if") {
			size_t chainEnd = i;
			while (chainEnd + 1 < section->codeLines.size()) {
				CodeLine *next = section->codeLines[chainEnd + 1];
				auto nextInfo = controlHeaderInfo(next);
				if (!nextInfo)
					break;
				const std::string &nextKind = std::get<0>(*nextInfo);
				if (nextKind != "else if" && nextKind != "else")
					break;
				chainEnd++;
			}

			std::optional<size_t> selectedBranch;
			bool branchKnown = true;
			for (size_t k = i; k <= chainEnd; k++) {
				auto branchInfo = controlHeaderInfo(section->codeLines[k]);
				if (!branchInfo) {
					branchKnown = false;
					break;
				}
				const std::string &branchKind = std::get<0>(*branchInfo);
				Function *header = std::get<1>(*branchInfo);
				const auto &headerBindings = std::get<2>(*branchInfo);
				if (branchKind == "else") {
					if (!selectedBranch.has_value())
						selectedBranch = k;
					break;
				}
				if (header->arguments.size() < 2) {
					branchKnown = false;
					break;
				}
				CompileTimeValue conditionValue = evaluateCompileTimeValue(
					header->arguments[1], context, headerBindings, context.currentCodegenInstantiation
				);
				std::optional<bool> condition = compileTimeTruthiness(conditionValue);
				if (!condition.has_value()) {
					branchKnown = false;
					break;
				}
				if (*condition) {
					selectedBranch = k;
					break;
				}
			}

			if (branchKnown && selectedBranch.has_value()) {
				CodeLine *selectedLine = section->codeLines[*selectedBranch];
				if (selectedLine->sectionOpening)
					generateSectionCode(context, selectedLine->sectionOpening);
				i = chainEnd;
				continue;
			}
		}

		if (line->function) {
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
			generateFunctionCode(context, line->function);
		}
	}

	return true;
}

bool generateCode(ParseContext &context) {
	context.llvmContext = new llvm::LLVMContext();
	context.llvmModule = new llvm::Module("dynlex_module", *context.llvmContext);
	context.llvmBuilder = new llvm::IRBuilder<>(*context.llvmContext);
	if (context.options.emitSPIRV) {
		std::string error;
		std::unique_ptr<llvm::TargetMachine> targetMachine = createSPIRVTargetMachine(context, error);
		if (!targetMachine) {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "SPIR-V target not available: " + error, Range())
			);
			return false;
		}
		context.llvmModule->setDataLayout(targetMachine->createDataLayout());
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
