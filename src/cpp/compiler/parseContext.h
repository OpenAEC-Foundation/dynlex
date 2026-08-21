#pragma once
#include "bindingResolution.h"
#include "codeLine.h"
#include "compilerUtils.h"
#include "diagnostic.h"
#include "lsp/fileSystem.h"
#include "patternMatch.h"
#include "patternTreeNode.h"
#include "section.h"
#include "syntaxConfig.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace llvm {
class LLVMContext;
class Module;
class IRBuilderBase;
class Value;
class GlobalVariable;
class Function;
class SwitchInst;
class BasicBlock;
class DIBuilder;
class DICompileUnit;
class DIFile;
class DIScope;
class TargetMachine;
} // namespace llvm

struct ParseContext {
	struct ShaderUniformSourceOrder {
		int mergedLineIndex = std::numeric_limits<int>::max();
		int column = std::numeric_limits<int>::max();
	};

	struct SectionFlexBodyFrame {
		Section *definitionSection = nullptr;
		InstantiatedSectionBody *definitionBody = nullptr;
		Section *bodySection = nullptr;
		InstantiatedSectionBody *instantiatedBody = nullptr;
		Expression *openingExpression = nullptr;
		llvm::BasicBlock *exitBlock = nullptr;
		llvm::BasicBlock *branchBackBlock = nullptr;
		llvm::BasicBlock *continuationBlock = nullptr;
		bool bodyEmitted = false;
	};

	struct ManagedStorageState {
		llvm::Value *address{};
		llvm::Value *initializedAddress{};
		DataType type;
		Section *ownerSection{};
	};

	enum class ShaderStage { Fragment, Vertex };
	enum class OptimizationSize { None, Size, Smallest };
	enum class FloatingPointContract { Off, Fast };

	// Installed only while pattern resolution owns the reference queues and
	// definition-to-reference index. Indexed pattern elements may be changed
	// only through this transaction.
	std::function<void(PatternDefinition &, const std::function<void()> &)> indexedPatternDefinitionMutation;
	// Highest compilation phase that completed successfully.
	// Guarantees by stage:
	// - NotStarted: no compiler-owned artifacts are guaranteed to exist.
	// - ImportedFiles: importedFiles, mainSourceFile, codeLines, and diagnostics gathered during file loading are valid.
	// - AnalyzedSections: mainSection exists and the section tree / CodeLine.section assignments are valid.
	// - ResolvedPatterns: patternTrees, pattern definitions, variable references, and pattern matches are valid.
	// - Validated: validation diagnostics that depend on resolved symbols have been emitted.
	// - InferredTypes: inferred expression / variable / return types are valid for the compiled program.
	enum class CompilationStage {
		NotStarted,
		ImportedFiles,
		AnalyzedSections,
		ResolvedPatterns,
		Validated,
		InferredTypes,
	};
	enum class SourceTokenKind {
		Keyword,
		Variable,
		Number,
		PatternReference,
	};

	struct SourceTokenAnnotation {
		Range range;
		SourceTokenKind kind;
		SectionType referencedPatternType = SectionType::Function;
	};

	struct Options {
		std::string inputPath;
		std::string outputPath;
		bool emitLLVM = false;
		bool emitWASM = false;
		bool emitSPIRV = false;
		ShaderStage shaderStage = ShaderStage::Fragment;
		int optimizationLevel = 0; // 0-3, corresponds to -O0 through -O3
		OptimizationSize optimizationSize = OptimizationSize::None;
		bool fastMath = false;
		FloatingPointContract floatingPointContract = FloatingPointContract::Off;
		std::optional<FloatingPointContract> explicitFloatingPointContract;
		std::optional<bool> loopVectorization;
		std::optional<bool> slpVectorization;
		std::optional<bool> loopUnrolling;
		std::string targetCPU = "generic";
		std::string targetTuneCPU;
		std::string targetFeatures;
		bool hasExplicitTargetConfiguration = false;
		// Maximum iterations for resolving pattern references and sections.
		// Pattern resolution is iterative: each pass resolves patterns that become unambiguous
		// when other patterns are resolved. 256 iterations is sufficient for deeply nested patterns.
		int maxResolutionIterations = 256;
		bool emitDebugInfo = false;
	} options;

	// LLVM

	// File system for reading source files (imports)
	// Owned by ParseContext so cached SourceFile objects outlive compilation
	std::unique_ptr<lsp::FileSystem> fileSystem;

	// LLVM codegen state (initialized in codegen.cpp)
	llvm::LLVMContext *llvmContext{};
	llvm::Module *llvmModule{};
	llvm::IRBuilderBase *llvmBuilder{};
	std::unique_ptr<llvm::TargetMachine> targetMachine;
	std::string resolvedTargetTuneCPU;
	llvm::Function *mainLLVMFunction{};
	llvm::BasicBlock *mainCleanupBlock{};
	llvm::AllocaInst *mainReturnStorage{};
	llvm::GlobalVariable *commandLineArgumentCountGlobal{};
	llvm::GlobalVariable *commandLineArgumentValuesGlobal{};

	// Debug info (initialized when emitDebugInfo is true, not for SPIR-V)
	llvm::DIBuilder *diBuilder{};
	llvm::DICompileUnit *diCompileUnit{};
	std::unordered_map<std::string, llvm::DIFile *> diFiles;
	llvm::DIScope *currentDebugScope{};

	// Temporary codegen bindings (pushed/popped during generation)
	// Pattern parameter bindings: maps variable name to LLVM value (for function parameters)
	std::unordered_map<std::string, llvm::Value *> patternBindings;
	// Pattern parameter types: maps parameter name to its type (for monomorphized functions)
	// Flex binding stack for flex expansion and variable resolution across nested flex scopes.
	BindingFrameStack flexBindingFrames;
	// Current body section for flex expansion (used by loop intrinsics to store loop info)
	Section *currentBodySection{};
	InstantiatedSectionBody *currentBodyInstantiation{};
	InstantiatedSectionBody *currentInstantiatedSectionBody{};
	// Active flex definition sections currently being expanded (outermost to innermost).
	// Used by execute-body ownership resolution when the intrinsic is wrapped through helper flexes.
	std::vector<Section *> activeFlexDefinitionStack;
	// Source sections for active flex call sites (outermost to innermost). This preserves
	// caller ownership when helper flexes wrap ownership-sensitive intrinsics.
	std::vector<Section *> flexCallSiteSectionStack;
	// Source ranges of the flex pattern calls currently being expanded, so
	// diagnostics from intrinsics inside flex replacement bodies can point at
	// the caller's line instead of the library definition.
	std::vector<Range> flexCallSiteRangeStack;
	// Active section-flex call frames. `execute body` resolves against this stack by
	// source ownership so nested section-flex expansions can emit the correct caller body.
	std::vector<SectionFlexBodyFrame> sectionFlexBodyFrames;
	// Current monomorphized function instantiation during codegen (for compile-time constants in conditions).
	const Instantiation *currentCodegenInstantiation{};
	// Most-refined type of each variable in the active monomorphized function or flex expansion.
	std::unordered_map<VariableReference *, DataType> finalizedVariableTypes;
	// Current switch statement being built (set by "switch" intrinsic, used by "case" intrinsic)
	llvm::SwitchInst *currentSwitchInst{};
	llvm::BasicBlock *currentSwitchExitBlock{};
	// Switches whose default block was already claimed by an `otherwise`
	// branch; keyed by identity so scoped state restoration cannot revive it.
	std::unordered_set<llvm::SwitchInst *> switchesWithDefaultCase;
	// Global variables (module-level, accessible across all DynLex files in the program)
	std::unordered_map<std::string, llvm::GlobalVariable *> globalLLVMVariables;
	// Subject values are call-site-local temporaries keyed by their inferred assignment expression.
	std::unordered_map<const Expression *, llvm::Value *> subjectStorage;
	// Runtime storage that owns values with class-defined or recursively derived
	// lifecycle behavior. Locals are scoped per generated function; globals are
	// released from main after all user code has completed.
	std::vector<ManagedStorageState> managedLocalStorage;
	std::vector<ManagedStorageState> managedGlobalStorage;

	// Libraries required for linking (collected from @intrinsic("call", ...) calls)
	std::unordered_set<std::string> requiredLibraries;

	// String constants (maps string content to global variable)
	std::unordered_map<std::string, llvm::GlobalVariable *> stringConstants;

	// Shader uniform names with stable parse-time source ordering metadata.
	// SPIR-V UBO fallback bindings are assigned from source location order, not codegen use order.
	std::vector<std::string> shaderUniformNames;
	std::unordered_map<std::string, ShaderUniformSourceOrder> shaderUniformSourceOrder;
	std::vector<std::string> shaderInterpolantNames;

	// imported source files by path (also prevents circular imports)
	std::unordered_map<std::string, lsp::SourceFile *> importedFiles;
	// The main source file (the one passed on the command line)
	lsp::SourceFile *mainSourceFile{};
	// Owns transformed/imported logical lines used throughout compilation.
	std::vector<std::unique_ptr<CodeLine>> ownedCodeLines;
	// all code lines in 'chronological' order: imported code lines get put before the import statement
	std::vector<CodeLine *> codeLines;
	std::vector<Diagnostic> diagnostics;
	CompilationStage compilationStage = CompilationStage::NotStarted;
	Section *mainSection{};
	// for each section type, we store a tree with patterns, leading to sections.
	// we use global pattern trees which can store multiple end nodes (exclusion based).
	// this is to prevent having to search all pattern trees of every scope, or merging trees per scope.
	PatternTreeNode *patternTrees[(int)SectionType::Count]{};
	// variable references that don't correspond to any pattern element
	std::unordered_map<std::string, std::list<VariableReference *>> unresolvedVariableReferences;
	// Owns all VariableReference instances for this compilation.
	// Other structures keep non-owning raw pointers into this arena.
	std::vector<std::unique_ptr<VariableReference>> ownedVariableReferences;
	// Compilation-lifetime arena for every expression allocated by an instance or
	// flex clone. Ownership is independent of mutable grouping-tree topology.
	std::vector<Expression *> ownedClonedExpressions;
	// Compiler-created conversion calls use matches that are not owned by a
	// source PatternReference.
	std::vector<std::unique_ptr<PatternMatch>> ownedSyntheticPatternMatches;
	// Type inference can select calls while evaluating operand-grouping trials.
	// Successful selections are materialized as ordinary calls when inference
	// finishes; stale trial entries have no inferred conversion at that point.
	std::vector<Expression *> expressionsWithInferredConversions;
	std::unordered_set<std::string> emittedOperandGroupingWarnings;
	// variable names declared as global (collected from globals: sections)
	std::unordered_set<std::string> declaredGlobalVariables;
	// Parse-time source token annotations for metadata syntax that is not represented as normal functions.
	std::vector<SourceTokenAnnotation> sourceTokenAnnotations;
	SyntaxConfig builtinSyntax;
	SyntaxConfig projectSyntax;
	std::string projectSyntaxConfigPath;
	// prohibit copies
	ParseContext(ParseContext &) = delete;
	ParseContext();
	~ParseContext();
	bool hasCompleted(CompilationStage stage) const { return compilationStage >= stage; }
	void addDiagnostic(Diagnostic diagnostic) { diagnostics.push_back(std::move(diagnostic)); }
	void addSourceToken(Range range, SourceTokenKind kind, SectionType referencedPatternType = SectionType::Function) {
		sourceTokenAnnotations.push_back({range, kind, referencedPatternType});
	}
	void printDiagnostics();
	PatternMatch *match(PatternReference *reference, MatchOptions options = {}, MatchDependencies *dependencies = nullptr);
	void processEncounteredIntrinsic(Expression *intrinsicExpr);
	void registerShaderUniformName(const std::string &uniformName, CodeLine *line = nullptr, int column = -1);
	void registerShaderInterpolantName(const std::string &interpolantName);
	VariableReference *createVariableReference(Range range, const std::string &name);
	Expression *cloneExpressionTree(Expression *expression, bool preserveInferenceMetadata = false);
	std::shared_ptr<InstantiatedSectionBody> cloneSectionBody(Section *section, bool preserveInferenceMetadata = false);
};

// Extract the body expression and parameter bindings from a flex PatternCall.
// If expr is a PatternCall to a flex section, returns the flex body's last expression
// and fills outBindings with parameter name → call-site argument expression.
// Returns nullptr if expr is not a flex PatternCall. Does not modify any binding stack —
// the caller decides how to apply the bindings (push onto codegen stack, or pass explicitly).
// Return every authored canonical path represented by one structural trie
// match. Callers must either evaluate all entries or use the path selected
// during overload inference; choosing the first entry loses choice semantics.
inline std::vector<size_t>
matchingPatternPathIndices(const std::vector<PatternTreeNode *> &nodesPassed, const PatternDefinition *definition) {
	requireCompilerInvariant(definition != nullptr, "matched path lookup requires a definition");
	requireCompilerInvariant(
		definition->indexedPaths.size() == definition->indexedNodePaths.size(),
		"matched path lookup requires complete indexed path metadata"
	);
	std::vector<size_t> pathIndices;
	for (size_t pathIndex = 0; pathIndex < definition->indexedNodePaths.size(); pathIndex++) {
		if (definition->indexedNodePaths[pathIndex] == nodesPassed)
			pathIndices.push_back(pathIndex);
	}
	requireCompilerInvariant(!pathIndices.empty(), "matched pattern nodes do not identify an indexed definition path");
	return pathIndices;
}

template <typename OnPatternParameterNameFn>
inline void forEachPatternParameterName(
	PatternDefinition *definition, size_t pathIndex, OnPatternParameterNameFn &&onPatternParameterName
) {
	requireCompilerInvariant(definition != nullptr, "pattern parameter traversal requires a definition");
	requireCompilerInvariant(
		pathIndex < definition->indexedPaths.size() && pathIndex < definition->indexedNodePaths.size(),
		"pattern parameter traversal received an invalid indexed path"
	);
	const auto &elements = definition->indexedPaths[pathIndex];
	const auto &nodes = definition->indexedNodePaths[pathIndex];
	requireCompilerInvariant(elements.size() == nodes.size(), "indexed pattern path metadata has the wrong size");
	for (size_t elementIndex = 0; elementIndex < elements.size(); elementIndex++) {
		const PatternElement &element = elements[elementIndex];
		if (element.type != PatternElement::Type::Variable && element.type != PatternElement::Type::Word)
			continue;
		onPatternParameterName(element.text, nodes[elementIndex], element.startPos);
	}
}

template <typename OnPatternBindingFn>
inline void forEachPatternCallBindingOnPath(
	Expression *expr, PatternDefinition *definition, size_t pathIndex, OnPatternBindingFn &&onPatternBinding
) {
	requireCompilerInvariant(
		expr && definition && expr->patternMatch, "pattern call binding requires a matched expression and definition"
	);
	requireCompilerInvariant(
		pathIndex < definition->indexedNodePaths.size() &&
			definition->indexedNodePaths[pathIndex] == expr->patternMatch->nodesPassed,
		"pattern call binding path does not match the expression"
	);
	size_t argIndex = 0;
	forEachPatternParameterName(definition, pathIndex, [&](const std::string &parameterName, PatternTreeNode *, size_t) {
		requireCompilerInvariant(argIndex < expr->arguments.size(), "pattern call has fewer arguments than parameters");
		onPatternBinding(parameterName, expr->arguments[argIndex++]);
	});
	requireCompilerInvariant(argIndex == expr->arguments.size(), "pattern call has more arguments than parameters");
}

template <typename OnPatternBindingFn>
inline void forEachPatternCallBinding(Expression *expr, PatternDefinition *definition, OnPatternBindingFn &&onPatternBinding) {
	requireCompilerInvariant(
		expr && expr->selectedPatternDefinition == definition && expr->selectedPatternPathIndex.has_value(),
		"pattern call binding requires a selected definition path"
	);
	forEachPatternCallBindingOnPath(
		expr, definition, *expr->selectedPatternPathIndex, std::forward<OnPatternBindingFn>(onPatternBinding)
	);
}

inline VariableReference *findPatternParameterDefinition(PatternDefinition *definition, const std::string &parameterName) {
	if (!definition || !definition->section)
		return nullptr;
	auto definitionIt = definition->section->variableDefinitions.find(parameterName);
	if (definitionIt == definition->section->variableDefinitions.end())
		return nullptr;
	return normalizeBindingReference(definitionIt->second);
}

inline void collectPatternCallBindingPairs(
	Expression *expr, PatternDefinition *definition, std::vector<std::pair<std::string, Expression *>> &outBindings
) {
	forEachPatternCallBinding(expr, definition, [&](const std::string &parameterName, Expression *argumentExpression) {
		outBindings.push_back({parameterName, argumentExpression});
	});
}

inline void collectPatternCallBindingPairsForPath(
	Expression *expr, PatternDefinition *definition, size_t pathIndex,
	std::vector<std::pair<std::string, Expression *>> &outBindings
) {
	forEachPatternCallBindingOnPath(
		expr, definition, pathIndex,
		[&](const std::string &parameterName, Expression *argumentExpression) {
		outBindings.push_back({parameterName, argumentExpression});
	}
	);
}

inline void collectPatternCallBindings(Expression *expr, PatternDefinition *definition, BindingMap &bindings) {
	forEachPatternCallBinding(expr, definition, [&](const std::string &parameterName, Expression *argumentExpression) {
		bindings[parameterName] = argumentExpression;
	});
}

inline void collectPatternCallBindings(Expression *expr, PatternDefinition *definition, BindingFrame &bindingFrame) {
	forEachPatternCallBinding(expr, definition, [&](const std::string &parameterName, Expression *argumentExpression) {
		bindingFrame.bindings[parameterName] = argumentExpression;
		if (VariableReference *parameterDefinition = findPatternParameterDefinition(definition, parameterName))
			bindingFrame.parameterBindings[parameterDefinition] = argumentExpression;
	});
}

inline void
pushPatternCallBindingScope(BindingFrameStack &bindingFrameStack, Expression *expression, PatternDefinition *definition) {
	BindingFrame bindings;
	collectPatternCallBindings(expression, definition, bindings);
	pushBindingScope(bindingFrameStack, std::move(bindings));
}

inline ResolvedBindingLayers
resolveExpressionBindingWithCallerScope(Expression *expression, const BindingFrameStack &bindingFrameStack) {
	if (!expression)
		return {nullptr, bindingFrameStack};
	if (expression->kind == Expression::Kind::Pending && expression->patternReference) {
		auto &elements = expression->patternReference->patternElements;
		if (elements.empty())
			elements = getPatternElements(expression->patternReference->pattern.text);
		if (elements.size() == 1 &&
			(elements[0].type == PatternElement::Type::Variable || elements[0].type == PatternElement::Type::VariableLike)) {
			return resolveNamedBindingWithCallerScope(elements[0].text, expression, bindingFrameStack);
		}
	}
	return resolveVariableBindingWithCallerScope(expression, bindingFrameStack);
}

struct FlexBindingExpansion {
	PatternDefinition *definition{};
	Expression *bodyExpression{};
};

inline std::optional<FlexBindingExpansion> selectedFlexBindingExpansion(Expression *expression) {
	if (!expression || !expression->inferredFlexExpansion)
		return std::nullopt;
	return FlexBindingExpansion{expression->selectedPatternDefinition, expression->inferredFlexExpansion};
}

template <typename SelectFlexExpansionFn, typename StopFn>
inline ResolvedBindingLayers resolveThroughBindingLayers(
	Expression *expression, BindingFrameStack bindingFrameStack, SelectFlexExpansionFn &&selectFlexExpansion, StopFn &&stop
) {
	std::unordered_set<Expression *> expandedFlexCalls;
	while (expression && !stop(expression)) {
		ResolvedBindingLayers bound = resolveExpressionBindingWithCallerScope(expression, bindingFrameStack);
		if (bound.expression != expression) {
			expression = bound.expression;
			bindingFrameStack = std::move(bound.bindingFrameStack);
			continue;
		}

		std::optional<FlexBindingExpansion> expansion = selectFlexExpansion(expression);
		if (!expansion)
			break;
		requireCompilerInvariant(
			expansion->definition && expansion->definition->section && expansion->definition->section->isFlex &&
				expansion->bodyExpression,
			"binding traversal received an invalid flex expansion"
		);
		requireCompilerInvariant(
			expandedFlexCalls.insert(expression).second, "binding traversal encountered a cyclic flex expansion"
		);
		pushPatternCallBindingScope(bindingFrameStack, expression, expansion->definition);
		expression = expansion->bodyExpression;
	}
	return {expression, std::move(bindingFrameStack)};
}

template <typename SelectFlexExpansionFn>
inline ResolvedBindingLayers resolveThroughBindingLayers(
	Expression *expression, BindingFrameStack bindingFrameStack, SelectFlexExpansionFn &&selectFlexExpansion
) {
	return resolveThroughBindingLayers(
		expression, std::move(bindingFrameStack), std::forward<SelectFlexExpansionFn>(selectFlexExpansion),
		[](Expression *) {
		return false;
	}
	);
}

inline Expression *flexPatternBodyExpression(PatternDefinition *definition, bool *isOnlyExpression = nullptr) {
	if (!definition || !definition->section || !definition->section->isFlex)
		return nullptr;
	Expression *bodyExpression = nullptr;
	size_t expressionCount = 0;
	definition->section->forEachDefinitionBodySection([&](Section *bodySection) {
		for (CodeLine *line : bodySection->codeLines) {
			if (line && line->expression) {
				bodyExpression = line->expression;
				expressionCount++;
			}
		}
		return true;
	});
	if (isOnlyExpression)
		*isOnlyExpression = expressionCount == 1;
	return bodyExpression;
}

inline Expression *singleExpressionFlexFunctionOutcome(PatternDefinition *definition) {
	if (!definition || !definition->section || !definition->section->isFlex ||
		definition->section->type != SectionType::Function)
		return nullptr;
	Expression *outcomeExpression = nullptr;
	size_t expressionLineCount = 0;
	definition->section->forEachDefinitionBodySection([&](Section *bodySection) {
		for (CodeLine *line : bodySection->codeLines) {
			if (!line || !line->expression)
				continue;
			expressionLineCount++;
			if (!line->sectionOpening)
				outcomeExpression = line->expression;
		}
		return true;
	});
	return expressionLineCount == 1 ? outcomeExpression : nullptr;
}

inline Expression *
expandFlexPatternCall(ParseContext &context, Expression *expr, PatternDefinition *def, BindingFrame &outBindings) {
	if (!expr || expr->kind != Expression::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return nullptr;
	auto &defs = expr->patternMatch->matchingDefinitions;
	if (def)
		requireCompilerInvariant(
			std::find(defs.begin(), defs.end(), def) != defs.end(),
			"expandFlexPatternCall received a definition that no longer matches the pattern call"
		);
	if (!def || !def->section || !def->section->isFlex)
		return nullptr;
	Expression *bodyExpr = flexPatternBodyExpression(def);
	if (!bodyExpr)
		return nullptr;
	collectPatternCallBindings(expr, def, outBindings);
	Expression *expandedBody = context.cloneExpressionTree(bodyExpr);
	if (expandedBody)
		expandedBody->isExplicitGroup = true;
	return expandedBody;
}
