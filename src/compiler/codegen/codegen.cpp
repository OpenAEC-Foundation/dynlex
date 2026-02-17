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
	const std::vector<Type> &argTypes, Instantiation &inst
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	std::vector<std::string> varNames;
	for (const auto &[name, expr] : paramBindings) {
		varNames.push_back(name);
	}

	// All parameters are opaque pointers
	std::vector<llvm::Type *> paramTypes(varNames.size(), llvm::PointerType::getUnqual(*context.llvmContext));

	// Return type: void for effects, per-instantiation for expressions
	llvm::Type *returnType;
	if (section->type == SectionType::Effect) {
		returnType = builder.getVoidTy();
	} else {
		auto it = section->instantiations.find(argTypes);
		assert(it != section->instantiations.end() && "Missing instantiation for arg types");
		assert(it->second.returnType.isDeduced() && "Return type must be deduced before codegen");
		returnType = getLLVMType(context, it->second.returnType);
	}

	llvm::FunctionType *funcType = llvm::FunctionType::get(returnType, paramTypes, false);

	// Name includes type signature for uniqueness
	std::string funcName = getPatternFunctionName(section);
	for (const Type &t : argTypes) {
		funcName += "_" + t.toString();
	}

	llvm::Function *func = llvm::Function::Create(funcType, llvm::Function::InternalLinkage, funcName, context.llvmModule);
	inst.llvmFunction = func;

	size_t argIdx = 0;
	for (auto &arg : func->args()) {
		arg.setName(varNames[argIdx++]);
	}

	llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context.llvmContext, "entry", func);

	// Save all codegen state
	llvm::BasicBlock *savedBlock = builder.GetInsertBlock();
	llvm::BasicBlock::iterator savedPoint = builder.GetInsertPoint();
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

	// Add return for effects (expression functions use @intrinsic("return"))
	if (section->type == SectionType::Effect) {
		builder.CreateRetVoid();
	}

	// Restore all codegen state
	context.macroExpressionBindings = context.macroBindingStack.top();
	context.macroBindingStack.pop();
	context.patternBindings = savedPatternBindings;
	context.patternParamTypes = savedParamTypes;

	if (savedBlock) {
		builder.SetInsertPoint(savedBlock, savedPoint);
	}
}

// Generate code for an expression
llvm::Value *generateExpressionCode(ParseContext &context, Expression *expr) {
	if (!expr)
		return nullptr;

	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	switch (expr->kind) {
	case Expression::Kind::Literal: {
		if (auto *intVal = std::get_if<int64_t>(&expr->literalValue)) {
			Type intType = getEffectiveType(context, expr);
			llvm::Type *llvmIntType = intType.toLLVM(*context.llvmContext);
			return llvm::ConstantInt::get(llvmIntType, *intVal, true);
		}
		if (auto *doubleVal = std::get_if<double>(&expr->literalValue)) {
			Type floatType = getEffectiveType(context, expr);
			llvm::Type *llvmFloatType = floatType.toLLVM(*context.llvmContext);
			return llvm::ConstantFP::get(llvmFloatType, *doubleVal);
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
		Expression *resolved = resolveMacroBinding(context, expr);
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
		Type varType = getEffectiveType(context, expr);

		// Class types: return the pointer directly (structs are passed by pointer)
		if (varType.kind == Type::Kind::Class) {
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

		PatternDefinition *matchedDef = expr->patternMatch->matchedEndNode->matchingDefinition;
		Section *matchedSection = matchedDef->section;
		if (!matchedSection)
			return nullptr;

		// Class type references are compile-time only — no runtime code
		if (matchedSection->type == SectionType::Class)
			return nullptr;

		// Sort arguments by source position (expandMatch appends submatches/variables/words
		// after direct args, so they may not be in text order)
		std::vector<Expression *> sortedArgs = sortArgumentsByPosition(expr->arguments);

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
		std::vector<Type> argTypes;
		for (const auto &[paramName, argExpr] : paramBindings) {
			argTypes.push_back(getEffectiveType(context, argExpr));
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
		if (line->expression)
			generateExpressionCode(context, line->expression);
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
