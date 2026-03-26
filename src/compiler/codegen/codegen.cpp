#include "codegen.h"
#include "bindingResolution.h"
#include "classDefinition.h"
#include "classSection.h"
#include "codegenInternal.h"
#include "compileTimeValue.h"
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
#include "wasm.h"
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

static std::vector<size_t> collectRuntimeParameterIndices(
	const std::vector<std::pair<std::string, Expression *>> &paramBindings, const Instantiation &inst
) {
	std::vector<size_t> runtimeIndices;
	runtimeIndices.reserve(paramBindings.size());
	for (size_t i = 0; i < paramBindings.size(); i++) {
		if (inst.requiredCompileTimeParameters.contains(paramBindings[i].first))
			continue;
		runtimeIndices.push_back(i);
	}
	return runtimeIndices;
}

// Generate a monomorphized LLVM function for a pattern definition with specific argument types.
// The Instantiation's llvmFunction is set before generating the body, enabling recursive calls.
Instantiation *generateSpecializedFunction(
	ParseContext &context, Section *section, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::vector<DataType> &argTypes
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	PatternDefinition *definition = section->patternDefinitions.empty() ? nullptr : section->patternDefinitions.front();
	auto evaluateParameterValue = [&](Expression *argumentExpression) {
		return argumentExpression
				   ? evaluateCompileTimeValue(
						 argumentExpression, context, context.macroBindingFrames, context.currentCodegenInstantiation
					 )
				   : CompileTimeValue{};
	};
	InstantiationKey instantiationKey =
		findMatchingInstantiationKey(section, paramBindings, argTypes, evaluateParameterValue)
			.value_or(buildInstantiationKey({}, paramBindings, argTypes, evaluateParameterValue));
	auto instIt = section->instantiations.find(instantiationKey);
	if (instIt == section->instantiations.end())
		instIt = section->instantiations.emplace(instantiationKey, Instantiation{}).first;
	Instantiation &inst = instIt->second;
	if (inst.argumentTypes.empty())
		inst.argumentTypes = argTypes;
	else
		assert(inst.argumentTypes == argTypes && "Instantiation argumentTypes diverged from map key");

	if (!inst.returnType.isDeduced() || inst.needsReinfer) {
		BindingMap callBindings;
		std::vector<std::string> parameterNames;
		for (const auto &[name, expr] : paramBindings)
			callBindings[name] = expr;
		for (const auto &[name, ignoredExpr] : paramBindings) {
			(void)ignoredExpr;
			parameterNames.push_back(name);
		}
		ensureSectionInstantiationInferred(
			context, section, definition, parameterNames, makeBindingFrameStack(callBindings), argTypes
		);
		instantiationKey = findMatchingInstantiationKey(section, paramBindings, argTypes, evaluateParameterValue)
							   .value_or(buildInstantiationKey({}, paramBindings, argTypes, evaluateParameterValue));
		instIt = section->instantiations.find(instantiationKey);
		assert(instIt != section->instantiations.end() && "Missing instantiation after inference");
	}
	Instantiation &activeInst = instIt->second;
	if (!activeInst.returnType.isDeduced()) {
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
	std::vector<size_t> runtimeParameterIndices = collectRuntimeParameterIndices(paramBindings, activeInst);
	std::vector<std::string> runtimeParameterNames;
	runtimeParameterNames.reserve(runtimeParameterIndices.size());
	for (size_t runtimeParameterIndex : runtimeParameterIndices)
		runtimeParameterNames.push_back(paramBindings[runtimeParameterIndex].first);

	// All runtime parameters are opaque pointers. Compile-time-only parameters
	// stay in patternParamTypes but do not become LLVM function arguments.
	std::vector<llvm::Type *> paramTypes(runtimeParameterNames.size(), llvm::PointerType::getUnqual(*context.llvmContext));
	llvm::Type *returnType = getLLVMType(context, activeInst.returnType);

	llvm::FunctionType *funcType = llvm::FunctionType::get(returnType, paramTypes, false);

	// Name includes type signature for uniqueness
	std::string funcName = getPatternFunctionName(section);
	for (const DataType &t : argTypes) {
		funcName += "_" + t.toString();
	}

	llvm::Function *func = llvm::Function::Create(funcType, llvm::Function::InternalLinkage, funcName, context.llvmModule);
	activeInst.llvmFunction = func;

	size_t argIdx = 0;
	for (auto &arg : func->args()) {
		arg.setName(runtimeParameterNames[argIdx++]);
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
	// Push macro bindings — function bodies must not see caller's macro bindings.
	pushClearedBindingScope(context.macroBindingFrames);

	builder.SetInsertPoint(entry);

	// Set up bindings: map parameter names to LLVM values and their types
	context.patternBindings.clear();
	context.patternParamTypes.clear();
	assert(activeInst.argumentTypes == argTypes && "Codegen argTypes diverged from instantiation signature");
	for (size_t i = 0; i < paramBindings.size() && i < argTypes.size(); i++)
		context.patternParamTypes[paramBindings[i].first] = argTypes[i];
	argIdx = 0;
	for (auto &arg : func->args()) {
		size_t parameterIndex = runtimeParameterIndices[argIdx];
		context.patternBindings[paramBindings[parameterIndex].first] = &arg;
		argIdx++;
	}
	context.currentCodegenInstantiation = &activeInst;

	// Generate function body
	for (Section *child : section->children) {
		generateSectionCode(context, child);
	}

	// Add implicit void return if the function returns void
	if (activeInst.returnType.kind == DataType::Kind::Void) {
		builder.CreateRetVoid();
	}

	// Restore all codegen state
	popBindingScopeOrFail(context.macroBindingFrames, "Missing macro binding scope when restoring codegen state");
	context.patternBindings = savedPatternBindings;
	context.patternParamTypes = savedParamTypes;
	context.currentCodegenInstantiation = savedCodegenInstantiation;
	context.currentDebugScope = savedDebugScope;

	if (savedBlock) {
		builder.SetInsertPoint(savedBlock, savedPoint);
		builder.SetCurrentDebugLocation(savedDebugLoc);
	}
	return &activeInst;
}

static DataType concretizeCallableType(DataType type) {
	if (type.kind == DataType::Kind::Class && type.classDefinition && type.classInstIndex < 0 &&
		!type.classDefinition->instantiations.empty()) {
		type.classInstIndex = 0;
	}
	return type;
}

static void collectCallablePatternParameters(
	const std::vector<DefinitionPatternElement> &elements, std::vector<std::pair<std::string, DataType>> &outParameters
) {
	for (const DefinitionPatternElement &element : elements) {
		switch (element.type) {
		case PatternElement::Type::Choice:
			if (!element.alternatives.empty())
				collectCallablePatternParameters(element.alternatives[0], outParameters);
			break;
		case PatternElement::Type::Variable:
			outParameters.push_back({element.text, concretizeCallableType(element.resolvedTypeConstraint)});
			break;
		default:
			break;
		}
	}
}

static bool buildCallableFunctionSignature(
	ParseContext &context, PatternDefinition *definition, std::vector<std::pair<std::string, DataType>> &outParameters
) {
	if (!definition || !definition->section)
		return false;

	outParameters.clear();
	collectCallablePatternParameters(definition->patternElements, outParameters);
	for (const auto &[parameterName, parameterType] : outParameters) {
		if (!parameterType.isDeduced()) {
			Diagnostic diagnostic;
			diagnostic.level = Diagnostic::Level::Error;
			diagnostic.range = definition->range;
			diagnostic.message = "function reference requires concrete parameter types: " + definition->toString() +
								 " parameter '" + parameterName + "'";
			context.addDiagnostic(std::move(diagnostic));
			return false;
		}
		if (parameterType.kind == DataType::Kind::Type || parameterType.kind == DataType::Kind::Void) {
			Diagnostic diagnostic;
			diagnostic.level = Diagnostic::Level::Error;
			diagnostic.range = definition->range;
			diagnostic.message = "function reference requires runtime parameters: " + definition->toString() + " parameter '" +
								 parameterName + "'";
			context.addDiagnostic(std::move(diagnostic));
			return false;
		}
	}
	return true;
}

static std::string buildCallableFunctionName(PatternDefinition *definition, const std::vector<DataType> &argTypes) {
	std::string name = getPatternFunctionName(definition->section) + "_callable";
	for (const DataType &type : argTypes)
		name += "_" + type.toString();
	return name;
}

static std::unique_ptr<Expression> makeCallablePlaceholderExpression(const DataType &type) {
	auto expression = std::make_unique<Expression>();
	expression->type = type;
	return expression;
}

llvm::Function *
ensureCallableFunctionGenerated(ParseContext &context, PatternDefinition *definition, bool requireExternalLinkage) {
	if (!definition || !definition->section || definition->section->type != SectionType::Function ||
		definition->section->isMacro) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "function reference requires non-macro function",
			definition ? definition->range : Range()
		));
		return nullptr;
	}
	if (context.options.emitSPIRV) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "function references are unavailable for SPIR-V targets", definition->range
		));
		return nullptr;
	}

	std::vector<std::pair<std::string, DataType>> parameters;
	if (!buildCallableFunctionSignature(context, definition, parameters))
		return nullptr;

	std::vector<std::pair<std::string, Expression *>> paramBindings;
	std::vector<std::unique_ptr<Expression>> ownedBindings;
	std::vector<DataType> argTypes;
	paramBindings.reserve(parameters.size());
	ownedBindings.reserve(parameters.size());
	argTypes.reserve(parameters.size());
	for (const auto &[parameterName, parameterType] : parameters) {
		argTypes.push_back(parameterType);
		auto placeholder = makeCallablePlaceholderExpression(parameterType);
		paramBindings.push_back({parameterName, placeholder.get()});
		ownedBindings.push_back(std::move(placeholder));
	}

	Section *section = definition->section;
	InstantiationKey instantiationKey = buildInstantiationKey({}, paramBindings, argTypes, [](Expression *) {
		return CompileTimeValue{};
	});
	Instantiation *inst = &section->instantiations[instantiationKey];
	if (inst->argumentTypes.empty()) {
		inst->argumentTypes = argTypes;
	} else {
		assert(inst->argumentTypes == argTypes && "Callable signature diverged from instantiation key");
	}
	if (!inst->llvmFunction) {
		inst = generateSpecializedFunction(context, section, paramBindings, argTypes);
		assert(inst && "Missing generated instantiation");
	}
	if (!inst->returnType.isDeduced() || !inst->valid) {
		Diagnostic diagnostic;
		diagnostic.level = Diagnostic::Level::Error;
		diagnostic.range = definition->range;
		diagnostic.message = "callable function inference failed: " + definition->toString();
		context.addDiagnostic(std::move(diagnostic));
		return nullptr;
	}
	if (inst->needsReinfer || !inst->requiredCompileTimeParameters.empty()) {
		Diagnostic diagnostic;
		diagnostic.level = Diagnostic::Level::Error;
		diagnostic.range = definition->range;
		diagnostic.message = "callable function cannot require compile-time parameters: " + definition->toString();
		context.addDiagnostic(std::move(diagnostic));
		return nullptr;
	}
	if (inst->llvmCallableFunction) {
		if (requireExternalLinkage)
			inst->llvmCallableFunction->setLinkage(llvm::GlobalValue::ExternalLinkage);
		return inst->llvmCallableFunction;
	}

	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	std::vector<llvm::Type *> parameterTypes;
	parameterTypes.reserve(argTypes.size());
	for (const DataType &parameterType : argTypes)
		parameterTypes.push_back(getLLVMType(context, parameterType));
	llvm::Type *returnType = getLLVMType(context, inst->returnType);
	llvm::FunctionType *callableType = llvm::FunctionType::get(returnType, parameterTypes, false);
	std::string callableName = buildCallableFunctionName(definition, argTypes);
	llvm::GlobalValue::LinkageTypes linkage = (requireExternalLinkage || section->isExposed)
												  ? llvm::GlobalValue::ExternalLinkage
												  : llvm::GlobalValue::InternalLinkage;
	llvm::Function *callableFunction = llvm::Function::Create(callableType, linkage, callableName, context.llvmModule);
	inst->llvmCallableFunction = callableFunction;

	size_t argumentIndex = 0;
	for (llvm::Argument &argument : callableFunction->args())
		argument.setName(parameters[argumentIndex++].first);

	llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context.llvmContext, "entry", callableFunction);
	llvm::BasicBlock *savedBlock = builder.GetInsertBlock();
	llvm::BasicBlock::iterator savedPoint = builder.GetInsertPoint();
	llvm::DebugLoc savedDebugLoc = builder.getCurrentDebugLocation();
	builder.SetInsertPoint(entry);

	std::vector<llvm::Value *> callArguments;
	callArguments.reserve(argTypes.size());
	argumentIndex = 0;
	for (llvm::Argument &argument : callableFunction->args()) {
		llvm::AllocaInst *parameterAlloca =
			createEntryAlloca(context, parameters[argumentIndex].first, argTypes[argumentIndex]);
		builder.CreateAlignedStore(&argument, parameterAlloca, llvm::Align(8));
		callArguments.push_back(parameterAlloca);
		argumentIndex++;
	}

	llvm::CallInst *call = builder.CreateCall(inst->llvmFunction, callArguments);
	if (inst->returnType.kind == DataType::Kind::Void) {
		builder.CreateRetVoid();
	} else {
		builder.CreateRet(call);
	}

	if (savedBlock) {
		builder.SetInsertPoint(savedBlock, savedPoint);
		builder.SetCurrentDebugLocation(savedDebugLoc);
	}

	return callableFunction;
}

static bool generateExposedFunctions(ParseContext &context, Section *section) {
	if (!section)
		return true;

	if (section->isExposed) {
		if (section->type != SectionType::Function || section->isMacro || section->patternDefinitions.empty()) {
			context.addDiagnostic(Diagnostic(
				context, Diagnostic::Level::Error, "exposed applies only to non-macro functions",
				section->openingLine ? Range(section->openingLine, section->openingLine->patternText) : Range()
			));
			return false;
		}
		if (!ensureCallableFunctionGenerated(context, section->patternDefinitions.front(), true))
			return false;
	}

	for (Section *child : section->children) {
		if (!generateExposedFunctions(context, child))
			return false;
	}
	return true;
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

	case Expression::Kind::ArrayLiteral: {
		DataType arrayType = getEffectiveType(context, expr);
		if (arrayType.kind != DataType::Kind::Array || !arrayType.arrayElementType)
			return nullptr;
		llvm::Type *llvmArrayType = getLLVMType(context, arrayType);
		llvm::AllocaInst *tempAlloca = createEntryAlloca(context, "array_literal", arrayType);
		for (size_t i = 0; i < expr->arguments.size(); i++) {
			llvm::Value *elementValue = generateExpressionCode(context, expr->arguments[i]);
			DataType fromType = getEffectiveType(context, expr->arguments[i]);
			elementValue = ensureType(context, elementValue, fromType, *arrayType.arrayElementType);
			llvm::Value *elementPtr = builder.CreateGEP(
				llvmArrayType, tempAlloca, {builder.getInt64(0), builder.getInt64(static_cast<int64_t>(i))}, "array_elem_ptr"
			);
			builder.CreateStore(elementValue, elementPtr);
		}
		return builder.CreateAlignedLoad(llvmArrayType, tempAlloca, llvm::Align(8), "array_literal_load");
	}

	case Expression::Kind::Variable: {
		Expression *resolved = resolveVariableBinding(context, expr);
		if (resolved != expr) {
			MacroScopeGuard guard(context);
			if (context.macroBindingFrames.hasParentScope())
				guard.popToCallerScope();
			return generateExpressionCode(context, resolved);
		}

		if (!expr->variable)
			return nullptr;
		std::string varName = expr->variable->name;

		// Determine this variable's type for loading
		DataType varType = getEffectiveType(context, expr);

		llvm::Type *loadType = getLLVMType(context, varType);

		// Pattern parameter: load from generated function parameter pointer
		auto bindingIt = context.patternBindings.find(varName);
		if (bindingIt != context.patternBindings.end())
			return builder.CreateAlignedLoad(loadType, bindingIt->second, llvm::Align(8), varName + "_val");

		// Local variable: load from alloca
		VariableReference *varRef = expr->variable;
		VariableReference *definition = varRef->definition ? varRef->definition : varRef;
		if (definition->alloca)
			return builder.CreateAlignedLoad(loadType, definition->alloca, llvm::Align(8), varName + "_val");

		context.addDiagnostic(Diagnostic(context, Diagnostic::Level::Error, "unknown variable", expr->range, "name", varName));
		return nullptr;
	}

	case Expression::Kind::PatternCall: {
		if (!expr->patternMatch || !expr->patternMatch->matchedEndNode)
			return nullptr;

		auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
		if (defs.empty())
			return nullptr;

		PatternDefinition *matchedDef = selectCodegenOverload(context, expr);
		assert(matchedDef && "Pattern call missing overload selection from type inference");
		assert(std::find(defs.begin(), defs.end(), matchedDef) != defs.end() && "Selected overload no longer matches call");
		assert(std::find(defs.begin(), defs.end(), matchedDef) != defs.end() && "Selected overload no longer matches call");
		assert(matchedDef && "No overload matched during codegen");

		Section *matchedSection = matchedDef->section;
		assert(matchedSection && "Selected overload has no section");

		// Non-macro class type references are compile-time only — no runtime code.
		// Macro class sections (primitive type definitions) fall through to macro expansion.
		if (matchedSection->type == SectionType::Class && !matchedSection->isMacro) {
			return nullptr;
		}

		// Build parameter name → argument expression mapping
		std::vector<std::pair<std::string, Expression *>> paramBindings;
		collectPatternCallBindingPairs(expr, matchedDef, paramBindings);
		if (matchedSection->isMacro && matchedSection->type == SectionType::Function) {
			BindingMap innerBindings;
			collectPatternCallBindings(expr, matchedDef, innerBindings);
			Expression *bodyExpr = expr->inferredMacroExpansion
									   ? context.cloneMacroExpansionExpression(expr->inferredMacroExpansion)
									   : expandMacroPatternCall(context, expr, matchedDef, innerBindings);
			if (!bodyExpr)
				return nullptr;

			pushBindingScope(context.macroBindingFrames, std::move(innerBindings));
			llvm::Value *result = generateExpressionCode(context, bodyExpr);
			popBindingScopeOrFail(context.macroBindingFrames, "Missing macro binding scope after function macro codegen");
			return result;
		}

		if (matchedSection->isMacro) {
			// Macro: inline the body with expression substitution.
			// Push current bindings and set only this macro's parameters (scoped).
			pushClearedBindingScope(context.macroBindingFrames);
			Section *savedBodySection = context.currentBodySection;

			for (const auto &[paramName, argExpr] : paramBindings) {
				context.macroBindingFrames.topBindings()[paramName] = argExpr;
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

			popBindingScopeOrFail(context.macroBindingFrames, "Missing macro binding scope after macro pattern call");
			context.currentBodySection = savedBodySection;
			return result;
		}

		// Non-macro pattern: monomorphized function call.
		// Compute argument types at this call site for specialization.
		std::vector<DataType> argTypes;
		for (const auto &[paramName, argExpr] : paramBindings) {
			DataType t = getEffectiveType(context, argExpr);
			if (!t.isDeduced() && matchedDef) {
				for (const auto &elem : matchedDef->patternElements) {
					if (elem.type == PatternElement::Type::Variable && elem.text == paramName &&
						elem.resolvedTypeConstraint.isDeduced()) {
						t = elem.resolvedTypeConstraint;
						break;
					}
				}
			}
			assert(t.isDeduced() && "Undeduced argument type at codegen");
			argTypes.push_back(t);
		}

		// Look up or generate the specialized function
		auto evaluateParameterValue = [&](Expression *argumentExpression) {
			return argumentExpression
					   ? evaluateCompileTimeValue(
							 argumentExpression, context, context.macroBindingFrames, context.currentCodegenInstantiation
						 )
					   : CompileTimeValue{};
		};
		InstantiationKey instantiationKey =
			findMatchingInstantiationKey(matchedSection, paramBindings, argTypes, evaluateParameterValue)
				.value_or(buildInstantiationKey({}, paramBindings, argTypes, evaluateParameterValue));
		Instantiation *inst = &matchedSection->instantiations[instantiationKey];
		if (inst->argumentTypes.empty()) {
			inst->argumentTypes = argTypes;
		} else {
			assert(inst->argumentTypes == argTypes && "Codegen instantiation signature diverged from lookup key");
		}
		if (!inst->llvmFunction) {
			inst = generateSpecializedFunction(context, matchedSection, paramBindings, argTypes);
			assert(inst && "Missing generated instantiation");
		}
		llvm::Function *func = inst->llvmFunction;

		// Build call arguments: pass variable pointers or temp allocas
		std::vector<llvm::Value *> args;
		for (size_t i = 0; i < paramBindings.size(); i++) {
			if (inst->requiredCompileTimeParameters.contains(paramBindings[i].first))
				continue;
			Expression *argExpr = paramBindings[i].second;
			llvm::Value *ptr = getVariablePointer(context, argExpr);
			if (ptr) {
				args.push_back(ptr);
			} else {
				llvm::Value *argVal = generateExpressionCode(context, argExpr);
				if (!argVal)
					crashCompilerBug("Runtime call argument produced no code");
				llvm::AllocaInst *tempAlloca = createEntryAlloca(context, "tmp", argTypes[i]);
				builder.CreateAlignedStore(argVal, tempAlloca, llvm::Align(8));
				args.push_back(tempAlloca);
			}
		}

		return builder.CreateCall(func, args);
	}

	case Expression::Kind::IntrinsicCall: {
		std::vector<Expression *> args(expr->arguments.begin() + 1, expr->arguments.end());
		return generateIntrinsicCode(context, expr->intrinsicName, args, getEffectiveType(context, expr));
	}

	case Expression::Kind::Pending:
		context.addDiagnostic(Diagnostic(context, Diagnostic::Level::Error, "unresolved pending expression", expr->range));
		return nullptr;

	case Expression::Kind::TypedPlaceholder:
		crashCompilerBug("Typed placeholder reached codegen");
	}

	return nullptr;
}

// Generate code for a section (process pattern references)
bool generateSectionCode(ParseContext &context, Section *section) {
	allocateSectionVariables(context, section);

	auto controlHeaderInfo = [&](CodeLine *line) -> std::optional<std::tuple<std::string, Expression *, BindingMap>> {
		if (!line || !line->expression)
			return std::nullopt;

		Expression *header = line->expression;
		BindingMap headerBindings;
		context.macroBindingFrames.forEachFrame([&headerBindings](const BindingFrame &frame) {
			for (const auto &[bindingName, expression] : frame.bindings)
				headerBindings[bindingName] = expression;
		});
		if (header->kind == Expression::Kind::PatternCall) {
			BindingMap innerBindings;
			Expression *expanded = expandMacroPatternCall(context, header, innerBindings);
			if (expanded) {
				header = expanded;
				for (const auto &[name, argExpr] : innerBindings)
					headerBindings[name] = argExpr;
			}
		}
		if (!header || header->kind != Expression::Kind::IntrinsicCall)
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

			if (context.currentCodegenInstantiation) {
				auto selectionIt = context.currentCodegenInstantiation->ifChainSelections.find(line);
				assert(
					selectionIt != context.currentCodegenInstantiation->ifChainSelections.end() &&
					"Missing per-instantiation if-chain selection from type inference"
				);
				const Instantiation::IfChainSelection &selection = selectionIt->second;
				if (selection.known) {
					if (selection.selectedBranchLine && selection.selectedBranchLine->sectionOpening)
						generateSectionCode(context, selection.selectedBranchLine->sectionOpening);
					i = chainEnd;
					continue;
				}
			} else {
				auto selectionIt = context.inferredIfChainSelections.find(line);
				assert(
					selectionIt != context.inferredIfChainSelections.end() &&
					"Missing non-instantiated if-chain selection from type inference"
				);
				const Instantiation::IfChainSelection &selection = selectionIt->second;
				if (selection.known) {
					if (selection.selectedBranchLine && selection.selectedBranchLine->sectionOpening)
						generateSectionCode(context, selection.selectedBranchLine->sectionOpening);
					i = chainEnd;
					continue;
				}
			}
		}

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
#ifdef DYNLEX_WEB
	if (!context.options.emitWASM) {
		context.addDiagnostic(
			Diagnostic(context, Diagnostic::Level::Error, "web build only supports --emit-wasm output mode", Range())
		);
		return false;
	}
	std::string targetError;
	std::unique_ptr<llvm::TargetMachine> targetMachine = createWASMTargetMachine(context, targetError);
	if (!targetMachine) {
		context.addDiagnostic(
			Diagnostic(context, Diagnostic::Level::Error, "wasm target not available", Range(), "error", targetError)
		);
		return false;
	}
	context.llvmModule->setDataLayout(targetMachine->createDataLayout());
#else
	if (context.options.emitSPIRV) {
		std::string error;
		std::unique_ptr<llvm::TargetMachine> targetMachine = createSPIRVTargetMachine(context, error);
		if (!targetMachine) {
			context.addDiagnostic(
				Diagnostic(context, Diagnostic::Level::Error, "spirv target not available", Range(), "error", error)
			);
			return false;
		}
		context.llvmModule->setDataLayout(targetMachine->createDataLayout());
	} else if (context.options.emitWASM) {
		std::string error;
		std::unique_ptr<llvm::TargetMachine> targetMachine = createWASMTargetMachine(context, error);
		if (!targetMachine) {
			context.addDiagnostic(
				Diagnostic(context, Diagnostic::Level::Error, "wasm target not available", Range(), "error", error)
			);
			return false;
		}
		context.llvmModule->setDataLayout(targetMachine->createDataLayout());
	} else {
		context.llvmModule->setTargetTriple(llvm::sys::getDefaultTargetTriple());
	}
#endif

	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	if (context.macroBindingFrames.empty())
		context.macroBindingFrames.pushFrame({});

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

	// Create main function: void main() for shaders, int main() for CPU/WASM
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

	if (!generateExposedFunctions(context, context.mainSection))
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
		context.addDiagnostic(Diagnostic(context, Diagnostic::Level::Error, "llvm verification failed", Range(), "error", error)
		);
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
#ifdef DYNLEX_WEB
	if (!context.options.emitWASM) {
		context.addDiagnostic(
			Diagnostic(context, Diagnostic::Level::Error, "web build only supports --emit-wasm output mode", Range())
		);
		return false;
	}
	if (!emitWASMModule(context))
		return false;
#else
	if (context.options.emitSPIRV) {
		if (!emitSPIRVModule(context))
			return false;
	} else if (context.options.emitWASM) {
		if (!emitWASMModule(context))
			return false;
	} else if (context.options.emitLLVM) {
		std::string outputPath = context.options.outputPath;
		if (outputPath.empty())
			outputPath = context.options.inputPath + ".ll";
		std::error_code ec;
		llvm::raw_fd_ostream out(outputPath, ec);
		if (ec) {
			context.addDiagnostic(
				Diagnostic(context, Diagnostic::Level::Error, "failed to open output file", Range(), "error", ec.message())
			);
			return false;
		}
		context.llvmModule->print(out, nullptr);
	} else {
		if (!emitNativeExecutable(context))
			return false;
	}
#endif

	return true;
}
