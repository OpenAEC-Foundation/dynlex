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
#include <limits>
#include <list>
#include <map>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace llvm {
class LLVMContext;
class Module;
class IRBuilderBase;
class Value;
class GlobalVariable;
class SwitchInst;
class BasicBlock;
class DIBuilder;
class DICompileUnit;
class DIFile;
class DIScope;
} // namespace llvm

struct ParseContext {
	struct ShaderUniformSourceOrder {
		int mergedLineIndex = std::numeric_limits<int>::max();
		int column = std::numeric_limits<int>::max();
	};

	struct SectionFlexBodyFrame {
		Section *definitionSection = nullptr;
		Section *bodySection = nullptr;
		InstantiatedSectionBody *instantiatedBody = nullptr;
		bool bodyEmitted = false;
	};

	enum class ShaderStage { Fragment, Vertex };
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
	// Current switch statement being built (set by "switch" intrinsic, used by "case" intrinsic)
	llvm::SwitchInst *currentSwitchInst{};
	llvm::BasicBlock *currentSwitchExitBlock{};
	// Switches whose default block was already claimed by an `otherwise`
	// branch; keyed by identity so scoped state restoration cannot revive it.
	std::unordered_set<llvm::SwitchInst *> switchesWithDefaultCase;
	// Global variables (module-level, accessible across all DynLex files in the program)
	std::unordered_map<std::string, llvm::GlobalVariable *> globalLLVMVariables;

	// Libraries required for linking (collected from @intrinsic("call", ...) calls)
	std::unordered_set<std::string> requiredLibraries;

	// String constants (maps string content to global variable)
	std::unordered_map<std::string, llvm::GlobalVariable *> stringConstants;

	// Shader uniform names with stable parse-time source ordering metadata.
	// SPIR-V UBO fallback bindings are assigned from source location order, not codegen use order.
	std::vector<std::string> shaderUniformNames;
	std::unordered_map<std::string, ShaderUniformSourceOrder> shaderUniformSourceOrder;

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
	// Precedence level assigned to function patterns not in the explicit precedence system.
	// Default-level patterns should not propagate minRightPrecedence constraints.
	int defaultPrecedenceLevel = 0;
	// variable references that don't correspond to any pattern element
	std::unordered_map<std::string, std::list<VariableReference *>> unresolvedVariableReferences;
	// Owns all VariableReference instances for this compilation.
	// Other structures keep non-owning raw pointers into this arena.
	std::vector<std::unique_ptr<VariableReference>> ownedVariableReferences;
	// Compilation-lifetime arena for every expression allocated by an instance or
	// flex clone. Ownership is independent of mutable grouping-tree topology.
	std::vector<Expression *> ownedClonedExpressions;
	std::unordered_set<std::string> emittedOperandGroupingWarnings;
	// variable names declared as global (collected from globals: sections)
	std::unordered_set<std::string> declaredGlobalVariables;
	// User-facing aliases for concrete types discovered from flex replacements like @intrinsic("type", ...).
	std::map<DataType, std::string> typeAliasNames;
	// Parse-time source token annotations for metadata syntax that is not represented as normal functions.
	std::vector<SourceTokenAnnotation> sourceTokenAnnotations;
	SyntaxConfig builtinSyntax;
	SyntaxConfig projectSyntax;
	std::string projectSyntaxConfigPath;
	// prohibit copies
	ParseContext(ParseContext &) = delete;
	ParseContext() = default;
	~ParseContext();
	bool hasCompleted(CompilationStage stage) const { return compilationStage >= stage; }
	void addDiagnostic(Diagnostic diagnostic) { diagnostics.push_back(std::move(diagnostic)); }
	void addSourceToken(Range range, SourceTokenKind kind, SectionType referencedPatternType = SectionType::Function) {
		sourceTokenAnnotations.push_back({range, kind, referencedPatternType});
	}
	void printDiagnostics();
	PatternMatch *match(PatternReference *reference, MatchOptions options = {});
	void processEncounteredIntrinsic(Expression *intrinsicExpr);
	void registerShaderUniformName(const std::string &uniformName, CodeLine *line = nullptr, int column = -1);
	VariableReference *createVariableReference(Range range, const std::string &name);
	Expression *cloneExpressionTree(Expression *expression, bool preserveInferenceMetadata = false);
	std::shared_ptr<InstantiatedSectionBody> cloneSectionBody(Section *section, bool preserveInferenceMetadata = false);
};

// Extract the body expression and parameter bindings from a flex PatternCall.
// If expr is a PatternCall to a flex section, returns the flex body's last expression
// and fills outBindings with parameter name → call-site argument expression.
// Returns nullptr if expr is not a flex PatternCall. Does not modify any binding stack —
// the caller decides how to apply the bindings (push onto codegen stack, or pass explicitly).
template <typename OnPatternParameterNameFn>
inline void forEachPatternParameterName(
	const std::vector<PatternTreeNode *> &nodesPassed, PatternDefinition *definition,
	OnPatternParameterNameFn &&onPatternParameterName
) {
	if (!definition)
		return;
	std::vector<std::tuple<size_t, PatternTreeNode *, std::string>> orderedParameters;
	for (PatternTreeNode *node : nodesPassed) {
		if (!node)
			continue;
		auto paramIt = node->parameterNames.find(definition);
		auto startIt = node->definitionStartPositions.find(definition);
		if (paramIt == node->parameterNames.end() || startIt == node->definitionStartPositions.end())
			continue;
		orderedParameters.push_back({startIt->second, node, paramIt->second});
	}
	std::sort(orderedParameters.begin(), orderedParameters.end(), [](const auto &left, const auto &right) {
		return std::get<0>(left) < std::get<0>(right);
	});
	for (const auto &[ignoredStartPos, node, parameterName] : orderedParameters)
		onPatternParameterName(parameterName, node);
}

template <typename OnPatternBindingFn>
inline void forEachPatternCallBinding(Expression *expr, PatternDefinition *definition, OnPatternBindingFn &&onPatternBinding) {
	if (!expr || !definition || !expr->patternMatch)
		return;
	size_t argIndex = 0;
	forEachPatternParameterName(
		expr->patternMatch->nodesPassed, definition,
		[&](const std::string &parameterName, PatternTreeNode *) {
		if (argIndex < expr->arguments.size())
			onPatternBinding(parameterName, expr->arguments[argIndex++]);
	}
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

inline Expression *flexPatternBodyExpression(PatternDefinition *definition) {
	if (!definition || !definition->section || !definition->section->isFlex)
		return nullptr;
	Expression *bodyExpression = nullptr;
	definition->section->forEachDefinitionBodySection([&](Section *bodySection) {
		for (CodeLine *line : bodySection->codeLines) {
			if (line && line->expression)
				bodyExpression = line->expression;
		}
		return true;
	});
	return bodyExpression;
}

inline Expression *
expandFlexPatternCall(ParseContext &context, Expression *expr, PatternDefinition *def, BindingFrame &outBindings) {
	if (!expr || expr->kind != Expression::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return nullptr;
	auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
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
