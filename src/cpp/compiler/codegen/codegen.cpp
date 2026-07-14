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
#include <bit>
#include <unordered_map>
#include <unordered_set>

static std::vector<size_t> collectRuntimeParameterIndices(
	const Instantiation &instantiation, const std::vector<std::pair<std::string, Expression *>> &paramBindings
) {
	std::vector<size_t> runtimeIndices;
	runtimeIndices.reserve(paramBindings.size());
	size_t bindingCount = std::min(paramBindings.size(), instantiation.argumentTypes.size());
	for (size_t i = 0; i < bindingCount; i++) {
		if (parameterRequiresCompileTimeInstantiationValue(
				instantiation.requiredCompileTimeParameters, paramBindings[i].first, instantiation.argumentTypes[i]
			))
			continue;
		runtimeIndices.push_back(i);
	}
	return runtimeIndices;
}

static std::string encodeInstantiationKeyForFunctionName(const InstantiationKey &instantiationKey) {
	std::string suffix;
	for (const auto &[name, value] : instantiationKey.compileTimeParameters) {
		suffix += "_ct_" + name + "_";
		if (const auto *number = std::get_if<double>(&value))
			suffix += std::to_string(std::bit_cast<uint64_t>(*number));
		else if (const auto *text = std::get_if<std::string>(&value))
			suffix += std::to_string(text->size()) + "_" + *text;
		else if (const auto *boolean = std::get_if<bool>(&value))
			suffix += *boolean ? "true" : "false";
		else if (const auto *typeRef = std::get_if<TypeReferenceValue>(&value))
			suffix += typeRef->type.toString();
		else if (const auto *constraint = std::get_if<TypeConstraint>(&value))
			suffix += constraint->toString();
		else
			suffix += "unknown";
	}
	return suffix;
}

namespace {
struct VariableAllocaSnapshotEntry {
	VariableReference *reference = nullptr;
	llvm::AllocaInst *alloca = nullptr;
};

static void collectVariableAllocaSnapshotEntries(
	Section *section, std::unordered_set<VariableReference *> &visitedReferences,
	std::vector<VariableAllocaSnapshotEntry> &entries
) {
	if (!section)
		return;
	for (const auto &[ignoredName, definitionReference] : section->variableDefinitions) {
		(void)ignoredName;
		if (!definitionReference || !visitedReferences.insert(definitionReference).second)
			continue;
		entries.push_back({definitionReference, definitionReference->alloca});
	}
	for (Section *child : section->children)
		collectVariableAllocaSnapshotEntries(child, visitedReferences, entries);
}

struct ScopedVariableAllocaRestore {
	std::vector<VariableAllocaSnapshotEntry> entries;

	explicit ScopedVariableAllocaRestore(Section *section) {
		std::unordered_set<VariableReference *> visitedReferences;
		collectVariableAllocaSnapshotEntries(section, visitedReferences, entries);
	}

	~ScopedVariableAllocaRestore() {
		for (const VariableAllocaSnapshotEntry &entry : entries) {
			requireCompilerInvariant(
				entry.reference != nullptr, "ScopedVariableAllocaRestore contains null definition reference"
			);
			entry.reference->alloca = entry.alloca;
		}
	}
};

struct ScopedActiveFlexDefinition {
	ParseContext &context;

	ScopedActiveFlexDefinition(ParseContext &ctx, Section *section) : context(ctx) {
		requireCompilerInvariant(section != nullptr, "ScopedActiveFlexDefinition requires a section");
		context.activeFlexDefinitionStack.push_back(section);
	}

	~ScopedActiveFlexDefinition() {
		requireCompilerInvariant(!context.activeFlexDefinitionStack.empty(), "Missing active flex definition to pop");
		context.activeFlexDefinitionStack.pop_back();
	}
};

struct ScopedFlexCallSiteSection {
	ParseContext &context;
	bool pushed = false;

	ScopedFlexCallSiteSection(ParseContext &ctx, Section *callSiteSection) : context(ctx) {
		if (!callSiteSection)
			return;
		context.flexCallSiteSectionStack.push_back(callSiteSection);
		pushed = true;
	}

	~ScopedFlexCallSiteSection() {
		if (!pushed)
			return;
		requireCompilerInvariant(!context.flexCallSiteSectionStack.empty(), "Missing flex call-site section to pop");
		context.flexCallSiteSectionStack.pop_back();
	}
};

static bool sectionIsDescendantOrSame(Section *section, Section *ancestor) {
	for (Section *current = section; current; current = current->parent) {
		if (current == ancestor)
			return true;
	}
	return false;
}

struct ScopedFlexCallSiteRange {
	ParseContext &context;

	ScopedFlexCallSiteRange(ParseContext &ctx, const Range &callRange) : context(ctx) {
		Range diagnosticRange = callRange;
		Section *callSection = callRange.line ? callRange.line->section : nullptr;
		if (!context.flexCallSiteRangeStack.empty() && !context.activeFlexDefinitionStack.empty() &&
			sectionIsDescendantOrSame(callSection, context.activeFlexDefinitionStack.back())) {
			diagnosticRange = context.flexCallSiteRangeStack.back();
		}
		context.flexCallSiteRangeStack.push_back(diagnosticRange);
	}

	~ScopedFlexCallSiteRange() {
		requireCompilerInvariant(!context.flexCallSiteRangeStack.empty(), "Missing flex call-site range to pop");
		context.flexCallSiteRangeStack.pop_back();
	}
};
} // namespace

// Generate a monomorphized LLVM function for a pattern definition with specific argument types.
// The Instantiation's llvmFunction is set before generating the body, enabling recursive calls.
Instantiation *generateSpecializedFunction(
	ParseContext &context, Section *section, PatternDefinition *,
	const std::vector<std::pair<std::string, Expression *>> &paramBindings, Instantiation &instantiation
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	auto instIt = std::find_if(section->instantiations.begin(), section->instantiations.end(), [&](const auto &entry) {
		return &entry.second == &instantiation;
	});
	requireCompilerInvariant(
		instIt != section->instantiations.end(), "selected codegen instantiation is not owned by its section"
	);
	const InstantiationKey &instantiationKey = instIt->first;
	Instantiation &activeInst = instIt->second;
	const std::vector<DataType> &argTypes = activeInst.argumentTypes;
	requireCompilerInvariant(activeInst.valid, "invalid function instantiation reached codegen");
	requireCompilerInvariant(!activeInst.inferring, "in-progress function instantiation reached codegen");
	requireCompilerInvariant(!activeInst.needsReinfer, "unfinished function instantiation reached codegen");
	requireCompilerInvariant(activeInst.returnType.isDeduced(), "function without a deduced return type reached codegen");
	requireCompilerInvariant(activeInst.body != nullptr, "function without an inferred body reached codegen");
	std::vector<size_t> runtimeParameterIndices = collectRuntimeParameterIndices(activeInst, paramBindings);
	std::vector<std::string> runtimeParameterNames;
	runtimeParameterNames.reserve(runtimeParameterIndices.size());
	for (size_t runtimeParameterIndex : runtimeParameterIndices)
		runtimeParameterNames.push_back(paramBindings[runtimeParameterIndex].first);

	std::vector<llvm::Type *> paramTypes(runtimeParameterNames.size(), llvm::PointerType::getUnqual(*context.llvmContext));
	llvm::Type *returnType = getLLVMType(context, activeInst.returnType);

	llvm::FunctionType *funcType = llvm::FunctionType::get(returnType, paramTypes, false);

	// Name includes type signature for uniqueness
	std::string funcName = getPatternFunctionName(section);
	for (const DataType &t : argTypes) {
		funcName += "_" + t.toString();
	}
	funcName += encodeInstantiationKeyForFunctionName(instantiationKey);

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
	const Instantiation *savedCodegenInstantiation = context.currentCodegenInstantiation;
	BindingFrameStack savedFlexBindingFrames = context.flexBindingFrames;
	// Function bodies must not see caller-side flex bindings or blame their
	// diagnostics on the caller's flex expansion.
	context.flexBindingFrames = makeBindingFrameStack(BindingFrame{});
	std::vector<Range> savedFlexCallSiteRanges = std::move(context.flexCallSiteRangeStack);
	context.flexCallSiteRangeStack.clear();

	builder.SetInsertPoint(entry);

	// Set up bindings: map parameter names to LLVM values and their types
	context.patternBindings.clear();
	requireCompilerInvariant(activeInst.argumentTypes == argTypes, "Codegen argTypes diverged from instantiation signature");
	argIdx = 0;
	for (auto &arg : func->args()) {
		size_t parameterIndex = runtimeParameterIndices[argIdx];
		context.patternBindings[paramBindings[parameterIndex].first] = &arg;
		argIdx++;
	}
	context.currentCodegenInstantiation = &activeInst;
	ScopedVariableAllocaRestore functionVariableAllocas(section);

	// Generate function body
	requireCompilerInvariant(
		activeInst.body != nullptr, "inferred function instantiation is missing its owned expression body"
	);
	// Parameters this call's match did not bind (their choice alternative was
	// not taken) live as locals of this instantiation and need storage here;
	// bound parameters are skipped because they resolve through patternBindings.
	allocateSectionVariables(context, section, activeInst.body.get());
	for (Section *child : section->children) {
		InstantiatedSectionBody *childBody = activeInst.body->bodyForChild(child);
		requireCompilerInvariant(childBody, "instantiated function body is missing a child section");
		generateSectionCode(context, child, childBody);
	}

	// Add implicit void return if the function returns void
	if (activeInst.returnType.kind == DataType::Kind::Void && !builder.GetInsertBlock()->getTerminator()) {
		builder.CreateRetVoid();
	}

	// Restore all codegen state
	context.flexBindingFrames = savedFlexBindingFrames;
	context.flexCallSiteRangeStack = std::move(savedFlexCallSiteRanges);
	context.patternBindings = savedPatternBindings;
	context.currentCodegenInstantiation = savedCodegenInstantiation;
	context.currentDebugScope = savedDebugScope;

	if (savedBlock) {
		builder.SetInsertPoint(savedBlock, savedPoint);
		builder.SetCurrentDebugLocation(savedDebugLoc);
	}
	return &activeInst;
}

static std::string buildCallableFunctionName(PatternDefinition *definition, const std::vector<DataType> &argTypes) {
	std::string name = getPatternFunctionName(definition->section) + "_callable";
	for (const DataType &type : argTypes)
		name += "_" + type.toString();
	return name;
}

llvm::Function *
ensureCallableFunctionGenerated(ParseContext &context, PatternDefinition *definition, bool requireExternalLinkage) {
	requireCompilerInvariant(
		definition && definition->section && definition->section->type == SectionType::Function && !definition->section->isFlex,
		"non-callable definition reached callable codegen"
	);
	requireCompilerInvariant(!context.options.emitSPIRV, "function reference reached SPIR-V codegen");

	std::vector<std::pair<std::string, DataType>> parameters;
	collectCallableFunctionParameters(definition, parameters);
	Instantiation *inst = definition->callableInstantiation;
	requireCompilerInvariant(inst, "callable definition reached codegen without its inferred instantiation");
	const std::vector<DataType> &argTypes = inst->argumentTypes;
	requireCompilerInvariant(parameters.size() == argTypes.size(), "callable parameter count changed after inference");

	std::vector<std::pair<std::string, Expression *>> paramBindings;
	paramBindings.reserve(parameters.size());
	for (size_t parameterIndex = 0; parameterIndex < parameters.size(); parameterIndex++) {
		requireCompilerInvariant(
			parameters[parameterIndex].second == argTypes[parameterIndex], "callable parameter type changed after inference"
		);
		paramBindings.push_back({parameters[parameterIndex].first, nullptr});
	}

	Section *section = definition->section;
	if (!inst->llvmFunction) {
		inst = generateSpecializedFunction(context, section, definition, paramBindings, *inst);
		requireCompilerInvariant(inst != nullptr, "Missing generated instantiation");
	}
	requireCompilerInvariant(inst->valid, "invalid callable instantiation reached codegen");
	requireCompilerInvariant(!inst->needsReinfer, "unfinished callable instantiation reached codegen");
	requireCompilerInvariant(inst->returnType.isDeduced(), "callable without a return type reached codegen");
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
		if (section->type != SectionType::Function || section->isFlex || section->patternDefinitions.empty()) {
			context.addDiagnostic(Diagnostic(
				context, Diagnostic::Level::Error, "exposed applies only to non-flex functions",
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

static void finalizeFlexBodySectionControlFlow(ParseContext &context, Section *bodySection) {
	if (!bodySection)
		return;
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	if (bodySection->exitBlock) {
		if (!builder.GetInsertBlock()->getTerminator()) {
			llvm::BasicBlock *target = bodySection->branchBackBlock ? bodySection->branchBackBlock : bodySection->exitBlock;
			builder.CreateBr(target);
		}
		builder.SetInsertPoint(bodySection->exitBlock);
	}
}

void emitFlexBodySection(ParseContext &context, Section *bodySection, InstantiatedSectionBody *body, bool finalizeControlFlow) {
	if (!bodySection)
		return;
	generateSectionCode(context, bodySection, body);
	if (finalizeControlFlow)
		finalizeFlexBodySectionControlFlow(context, bodySection);
}

static llvm::Value *generateStringConstant(ParseContext &context, const std::string &value) {
	// Strings are currently i8* pointers to constant data. Runtime string
	// operations remain the responsibility of the string library.
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	auto it = context.stringConstants.find(value);
	if (it != context.stringConstants.end()) {
		llvm::GlobalVariable *strGlobal = it->second;
		return builder.CreateInBoundsGEP(
			strGlobal->getValueType(), strGlobal, {builder.getInt64(0), builder.getInt64(0)}, "str_ptr"
		);
	}
	std::string globalName = ".str." + std::to_string(context.stringConstants.size());
	llvm::Constant *strConst = llvm::ConstantDataArray::getString(*context.llvmContext, value, true);
	llvm::GlobalVariable *strGlobal = new llvm::GlobalVariable(
		*context.llvmModule, strConst->getType(), true, llvm::GlobalValue::PrivateLinkage, strConst, globalName
	);
	context.stringConstants[value] = strGlobal;
	return builder.CreateInBoundsGEP(
		strGlobal->getValueType(), strGlobal, {builder.getInt64(0), builder.getInt64(0)}, "str_ptr"
	);
}

static llvm::Value *
generateCompileTimeRuntimeValue(ParseContext &context, const CompileTimeValue &value, const DataType &type) {
	requireCompilerInvariant(type.isRuntimeValueType(), "compile-time-only type reached runtime value codegen");
	llvm::Type *llvmType = getLLVMType(context, type);
	if (const auto *number = std::get_if<double>(&value)) {
		if (type.kind == DataType::Kind::Int)
			return llvm::ConstantInt::get(llvmType, static_cast<int64_t>(*number), true);
		if (type.kind == DataType::Kind::Float)
			return llvm::ConstantFP::get(llvmType, *number);
	}
	if (const auto *boolean = std::get_if<bool>(&value)) {
		requireCompilerInvariant(type.kind == DataType::Kind::Bool, "boolean compile-time value has non-boolean runtime type");
		return llvm::ConstantInt::get(llvmType, *boolean ? 1 : 0);
	}
	if (const auto *text = std::get_if<std::string>(&value)) {
		requireCompilerInvariant(type.isBytePointer(), "string compile-time value has non-string runtime type");
		return generateStringConstant(context, *text);
	}
	crashCompilerBug("compile-time parameter value cannot be represented at runtime");
}

// Generate code for an expression
llvm::Value *generateExpressionCode(ParseContext &context, Expression *expr) {
	if (!expr)
		return nullptr;

	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	switch (expr->kind) {
	case Expression::Kind::Literal: {
		if (auto *doubleVal = std::get_if<double>(&expr->literalValue)) {
			DataType numType = finalizedExpressionType(context, expr);
			llvm::Type *llvmType = numType.toLLVM(*context.llvmContext);
			if (numType.kind == DataType::Kind::Int)
				return llvm::ConstantInt::get(llvmType, (int64_t)*doubleVal, true);
			return llvm::ConstantFP::get(llvmType, *doubleVal);
		}
		if (auto *strVal = std::get_if<std::string>(&expr->literalValue)) {
			return generateStringConstant(context, *strVal);
		}
		// Unknown literal variant type - should never reach here after type inference
		crashCompilerBug("Unknown literal type in codegen");
	}

	case Expression::Kind::ArrayLiteral: {
		DataType arrayType = finalizedExpressionType(context, expr);
		if (arrayType.kind != DataType::Kind::Array || !arrayType.arrayElementType)
			return nullptr;
		llvm::Type *llvmArrayType = getLLVMType(context, arrayType);
		llvm::AllocaInst *tempAlloca = createEntryAlloca(context, "array_literal", arrayType);
		for (size_t i = 0; i < expr->arguments.size(); i++) {
			llvm::Value *elementValue = generateExpressionCode(context, expr->arguments[i]);
			DataType fromType = finalizedExpressionType(context, expr->arguments[i]);
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
			FlexScopeGuard guard(context);
			if (context.flexBindingFrames.hasParentScope())
				guard.popToCallerScope();
			return generateExpressionCode(context, resolved);
		}

		requireCompilerInvariant(expr->variable != nullptr, "Variable expression reached codegen without a resolved variable");
		std::string varName = expr->variable->name;

		// Determine this variable's type for loading
		DataType varType = finalizedExpressionType(context, expr);
		if (context.currentCodegenInstantiation &&
			context.currentCodegenInstantiation->requiredCompileTimeParameters.contains(varName)) {
			auto valueIt = context.currentCodegenInstantiation->constantParameterValues.find(varName);
			requireCompilerInvariant(
				valueIt != context.currentCodegenInstantiation->constantParameterValues.end(),
				"compile-time parameter reached codegen without its instantiation value"
			);
			return generateCompileTimeRuntimeValue(context, valueIt->second, varType);
		}

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

		crashCompilerBug("Variable '" + varName + "' reached codegen without storage");
	}

	case Expression::Kind::PatternCall: {
		requireCompilerInvariant(
			expr->patternMatch && expr->patternMatch->matchedEndNode,
			"pattern call reached codegen without its resolved pattern match"
		);

		auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
		requireCompilerInvariant(!defs.empty(), "pattern call reached codegen without matching definitions");

		PatternDefinition *matchedDef = finalizedPatternDefinition(context, expr);
		requireCompilerInvariant(matchedDef != nullptr, "Pattern call missing overload selection from type inference");
		requireCompilerInvariant(
			std::find(defs.begin(), defs.end(), matchedDef) != defs.end(), "Selected overload no longer matches call"
		);

		Section *matchedSection = matchedDef->section;
		requireCompilerInvariant(matchedSection != nullptr, "Selected overload has no section");

		// Non-flex class type references are compile-time only — no runtime code.
		// Flex class sections (primitive type definitions) fall through to flex expansion.
		if (matchedSection->type == SectionType::Class && !matchedSection->isFlex) {
			return nullptr;
		}

		// Build parameter name → argument expression mapping
		std::vector<std::pair<std::string, Expression *>> paramBindings;
		collectPatternCallBindingPairs(expr, matchedDef, paramBindings);
		if (matchedSection->isFlex) {
			// Flex: inline the body with expression substitution.
			// Push current bindings and set only this flex's parameters (scoped).
			BindingFrame innerBindings;
			collectPatternCallBindings(expr, matchedDef, innerBindings);
			pushBindingScope(context.flexBindingFrames, std::move(innerBindings));
			ScopedFlexCallSiteRange callSiteRangeScope(context, expr->range);
			ScopedVariableAllocaRestore flexVariableAllocas(matchedSection);
			ScopedActiveFlexDefinition activeFlexScope(context, matchedSection);
			Section *callSiteSection = expr->range.line ? expr->range.line->section : nullptr;
			ScopedFlexCallSiteSection callSiteScope(context, callSiteSection);
			Section *savedBodySection = context.currentBodySection;
			InstantiatedSectionBody *savedBodyInstantiation = context.currentBodyInstantiation;
			// Scope switch state to this expansion and its body, so a nested
			// match inside a case body cannot capture the outer match's cases.
			llvm::SwitchInst *savedSwitchInst = context.currentSwitchInst;
			llvm::BasicBlock *savedSwitchExitBlock = context.currentSwitchExitBlock;

			// Only section-type flexes (like "if condition:", "loop while condition:")
			// should pick up and process the body section opened by this line.
			// Function flexes (like "not value:", "a + b") must NOT process
			// the body section, even if they appear on a line that opens one.
			Section *bodySection = nullptr;
			InstantiatedSectionBody *bodyInstantiation = nullptr;
			if (matchedSection->type == SectionType::Section) {
				bodySection = expr->range.line ? expr->range.line->sectionOpening : nullptr;
				bodyInstantiation = context.currentInstantiatedSectionBody && bodySection
										? context.currentInstantiatedSectionBody->bodyForChild(bodySection)
										: nullptr;
				context.currentBodySection = bodySection;
				context.currentBodyInstantiation = bodyInstantiation;
				if (bodySection)
					context.sectionFlexBodyFrames.push_back({matchedSection, bodySection, bodyInstantiation, false});
			}

			llvm::Value *result = nullptr;
			requireCompilerInvariant(
				static_cast<bool>(expr->inferredFlexBody), "section flex reached codegen without its inferred replacement body"
			);
			matchedSection->forEachDefinitionBodySection([&](Section *definitionBodySection) {
				InstantiatedSectionBody *definitionBody = definitionBodySection == matchedSection
															  ? expr->inferredFlexBody.get()
															  : expr->inferredFlexBody->bodyForChild(definitionBodySection);
				requireCompilerInvariant(definitionBody, "section flex inferred body is missing a definition section");
				InstantiatedSectionBody *savedInstantiatedSectionBody = context.currentInstantiatedSectionBody;
				context.currentInstantiatedSectionBody = definitionBody;
				allocateSectionVariables(context, definitionBodySection, definitionBody);
				for (size_t lineIndex = 0; lineIndex < definitionBodySection->codeLines.size(); lineIndex++) {
					Expression *lineExpression = definitionBody->lineExpression(lineIndex);
					if (lineExpression)
						result = generateExpressionCode(context, lineExpression);
				}
				context.currentInstantiatedSectionBody = savedInstantiatedSectionBody;
				return true;
			});

			if (bodySection) {
				requireCompilerInvariant(
					!context.sectionFlexBodyFrames.empty(), "Missing section flex body frame when leaving section flex"
				);
				ParseContext::SectionFlexBodyFrame &frame = context.sectionFlexBodyFrames.back();
				requireCompilerInvariant(
					frame.definitionSection == matchedSection,
					"Section flex body frame stack diverged from active flex expansion"
				);
				if (!frame.bodyEmitted) {
					frame.bodyEmitted = true;
					emitFlexBodySection(context, frame.bodySection, frame.instantiatedBody);
				} else {
					finalizeFlexBodySectionControlFlow(context, frame.bodySection);
				}
				context.sectionFlexBodyFrames.pop_back();
			}

			popBindingScopeOrFail(context.flexBindingFrames, "Missing flex binding scope after flex pattern call");
			context.currentBodySection = savedBodySection;
			context.currentBodyInstantiation = savedBodyInstantiation;
			context.currentSwitchInst = savedSwitchInst;
			context.currentSwitchExitBlock = savedSwitchExitBlock;
			return result;
		}

		// Non-flex pattern: emit the exact monomorphized function selected by inference.
		Instantiation *inst = expr->selectedInstantiation;
		requireCompilerInvariant(inst != nullptr, "non-flex call reached codegen without its selected instantiation");
		const std::vector<DataType> &argTypes = inst->argumentTypes;
		requireCompilerInvariant(
			argTypes.size() == paramBindings.size(), "selected instantiation argument count diverged from the call"
		);
		if (!inst->llvmFunction) {
			inst = generateSpecializedFunction(context, matchedSection, matchedDef, paramBindings, *inst);
			requireCompilerInvariant(inst != nullptr, "Missing generated instantiation");
		}
		llvm::Function *func = inst->llvmFunction;

		// Build call arguments: pass variable pointers or temp allocas
		std::vector<size_t> runtimeParameterIndices = collectRuntimeParameterIndices(*inst, paramBindings);
		std::vector<llvm::Value *> args;
		args.reserve(runtimeParameterIndices.size());
		for (size_t runtimeParameterIndex : runtimeParameterIndices) {
			Expression *argExpr = paramBindings[runtimeParameterIndex].second;
			llvm::Value *ptr = getVariablePointer(context, argExpr);
			if (ptr) {
				args.push_back(ptr);
			} else {
				llvm::Value *argVal = generateExpressionCode(context, argExpr);
				if (!argVal)
					crashCompilerBug("Runtime call argument produced no code");
				llvm::AllocaInst *tempAlloca = createEntryAlloca(context, "tmp", argTypes[runtimeParameterIndex]);
				builder.CreateAlignedStore(argVal, tempAlloca, llvm::Align(8));
				args.push_back(tempAlloca);
			}
		}

		return builder.CreateCall(func, args);
	}

	case Expression::Kind::IntrinsicCall: {
		return generateIntrinsicCode(
			context, expr, expr->intrinsicName, expr->arguments, finalizedExpressionType(context, expr)
		);
	}

	case Expression::Kind::Pending:
		crashCompilerBug("Pending expression reached codegen");

	case Expression::Kind::TypedPlaceholder:
		crashCompilerBug("Typed placeholder reached codegen");
	}

	return nullptr;
}

// Generate code for a section (process pattern references)
bool generateSectionCode(ParseContext &context, Section *section, InstantiatedSectionBody *body) {
	requireCompilerInvariant(!body || body->sourceSection == section, "active codegen body does not match section");
	InstantiatedSectionBody *savedInstantiatedBody = context.currentInstantiatedSectionBody;
	context.currentInstantiatedSectionBody = body;
	struct InstantiatedBodyRestore {
		ParseContext &context;
		InstantiatedSectionBody *saved;
		~InstantiatedBodyRestore() { context.currentInstantiatedSectionBody = saved; }
	} instantiatedBodyRestore{context, savedInstantiatedBody};
	allocateSectionVariables(context, section, body);

	auto controlHeaderInfo = [&](CodeLine *line, Expression *lineExpression
							 ) -> std::optional<std::tuple<std::string, Expression *, BindingMap>> {
		if (!line || !lineExpression)
			return std::nullopt;

		Expression *header = lineExpression;
		BindingMap headerBindings;
		context.flexBindingFrames.forEachFrame([&headerBindings](const BindingFrame &frame) {
			for (const auto &[bindingName, expression] : frame.bindings)
				headerBindings[bindingName] = expression;
		});
		if (header->kind == Expression::Kind::PatternCall) {
			PatternDefinition *definition = finalizedPatternDefinition(context, header);
			if (!definition->section || !definition->section->isFlex)
				return std::nullopt;
			requireCompilerInvariant(
				header->inferredFlexExpansion, "control-flow flex reached codegen without its finalized expansion"
			);
			BindingMap innerBindings;
			collectPatternCallBindings(header, definition, innerBindings);
			header = header->inferredFlexExpansion;
			for (const auto &[name, argExpr] : innerBindings)
				headerBindings[name] = argExpr;
		}
		if (!header || header->kind != Expression::Kind::IntrinsicCall)
			return std::nullopt;
		if (header->intrinsicName != "if" && header->intrinsicName != "else if" && header->intrinsicName != "else")
			return std::nullopt;
		return std::make_optional(std::make_tuple(header->intrinsicName, header, std::move(headerBindings)));
	};
	for (size_t i = 0; i < section->codeLines.size(); i++) {
		CodeLine *line = section->codeLines[i];
		Expression *lineExpression = body ? body->lineExpression(i) : line->expression;
		auto headerInfo = controlHeaderInfo(line, lineExpression);
		if (headerInfo && std::get<0>(*headerInfo) == "if") {
			size_t chainEnd = i;
			while (chainEnd + 1 < section->codeLines.size()) {
				CodeLine *next = section->codeLines[chainEnd + 1];
				Expression *nextExpression = body ? body->lineExpression(chainEnd + 1) : next->expression;
				auto nextInfo = controlHeaderInfo(next, nextExpression);
				if (!nextInfo)
					break;
				const std::string &nextKind = std::get<0>(*nextInfo);
				if (nextKind != "else if" && nextKind != "else")
					break;
				chainEnd++;
			}

			requireCompilerInvariant(
				lineExpression && lineExpression->branchSelection,
				"if chain reached codegen without finalized branch-selection metadata"
			);
			const Expression::BranchSelection &selection = *lineExpression->branchSelection;
			if (selection.known) {
				if (selection.selectedBranchIndex >= 0) {
					requireCompilerInvariant(
						selection.selectedBranchIndex < static_cast<int>(section->codeLines.size()),
						"finalized if-chain branch index is out of range"
					);
					CodeLine *selectedLine = section->codeLines[selection.selectedBranchIndex];
					if (selectedLine->sectionOpening) {
						InstantiatedSectionBody *selectedBody =
							body ? body->bodyForChild(selectedLine->sectionOpening) : nullptr;
						generateSectionCode(context, selectedLine->sectionOpening, selectedBody);
					}
				}
				i = chainEnd;
				continue;
			}
		}

		if (lineExpression) {
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
			generateExpressionCode(context, lineExpression);
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
	if (context.flexBindingFrames.empty())
		context.flexBindingFrames.pushFrame(BindingFrame{});

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

	// No first pass — non-flex functions are generated on-demand via monomorphization.

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

	if (!builder.GetInsertBlock()->getTerminator()) {
		if (context.options.emitSPIRV) {
			builder.CreateRetVoid();
		} else {
			builder.CreateRet(builder.getInt32(0));
		}
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
