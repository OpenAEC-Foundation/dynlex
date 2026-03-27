#pragma once
#include "bindingResolution.h"
#include "codeLine.h"
#include "diagnostic.h"
#include "lsp/fileSystem.h"
#include "patternMatch.h"
#include "patternTreeNode.h"
#include "section.h"
#include "syntaxConfig.h"
#include <algorithm>
#include <cassert>
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

	struct SectionMacroBodyFrame {
		Section *definitionSection = nullptr;
		Section *bodySection = nullptr;
		bool bodyEmitted = false;
	};

	enum class ShaderStage { Fragment, Vertex };
	// Highest compilation phase that completed successfully.
	// Guarantees by stage:
	// - NotStarted: no compiler-owned artifacts are guaranteed to exist.
	// - ImportedFiles: importedFiles, mainSourceFile, codeLines, and diagnostics gathered during file loading are valid.
	// - AnalyzedSections: mainSection exists and the section tree / CodeLine.section assignments are valid.
	// - ResolvedPatterns: patternTrees, pattern definitions, variable references, and pattern matches are valid.
	// - ResolvedDeclaredTypes: declared class field types and declared class instantiations are resolved.
	// - Validated: validation diagnostics that depend on resolved symbols have been emitted.
	// - InferredTypes: inferred expression / variable / return types are valid for the compiled program.
	enum class CompilationStage {
		NotStarted,
		ImportedFiles,
		AnalyzedSections,
		ResolvedPatterns,
		ResolvedDeclaredTypes,
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

	// Debug info (initialized when emitDebugInfo is true, not for SPIR-V)
	llvm::DIBuilder *diBuilder{};
	llvm::DICompileUnit *diCompileUnit{};
	std::unordered_map<std::string, llvm::DIFile *> diFiles;
	llvm::DIScope *currentDebugScope{};

	// Temporary codegen bindings (pushed/popped during generation)
	// Pattern parameter bindings: maps variable name to LLVM value (for function parameters)
	std::unordered_map<std::string, llvm::Value *> patternBindings;
	// Pattern parameter types: maps parameter name to its type (for monomorphized functions)
	std::unordered_map<std::string, DataType> patternParamTypes;
	// Macro binding stack for macro expansion and variable resolution across nested macro scopes.
	BindingFrameStack macroBindingFrames;
	// Current body section for macro expansion (used by loop intrinsics to store loop info)
	Section *currentBodySection{};
	// Active macro definition sections currently being expanded (outermost to innermost).
	// Used by execute-body ownership resolution when the intrinsic is wrapped through helper macros.
	std::vector<Section *> activeMacroDefinitionStack;
	// Source sections for active macro call sites (outermost to innermost). This preserves
	// caller ownership when helper macros wrap ownership-sensitive intrinsics.
	std::vector<Section *> macroCallSiteSectionStack;
	// Active section-macro call frames. `execute body` resolves against this stack by
	// source ownership so nested section-macro expansions can emit the correct caller body.
	std::vector<SectionMacroBodyFrame> sectionMacroBodyFrames;
	// Current monomorphized function instantiation during codegen (for compile-time constants in conditions).
	const Instantiation *currentCodegenInstantiation{};
	// Current switch statement being built (set by "switch" intrinsic, used by "case" intrinsic)
	llvm::SwitchInst *currentSwitchInst{};
	llvm::BasicBlock *currentSwitchExitBlock{};
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
	// Owns cloned macro expansion roots so call-site-specific inference and grouping
	// never mutate shared macro definition expression trees.
	std::vector<Expression *> ownedMacroExpansionRoots;
	// Owns cloned macro argument captures where outer bindings must be frozen into
	// a nested argument expression to avoid self-capture across macro scopes.
	std::vector<Expression *> ownedCapturedBindingRoots;
	// Owns temporary literal expressions materialized during codegen when a
	// compile-time-only non-macro parameter must be inspected as an expression.
	std::vector<Expression *> ownedCodegenLiteralRoots;
	// Compile-time constants captured per variable reference for non-instantiated flows (e.g. main section).
	std::unordered_map<VariableReference *, CompileTimeValue> constantValuesByReference;
	std::unordered_map<CodeLine *, Instantiation::IfChainSelection> inferredIfChainSelections;
	std::unordered_set<std::string> emittedOperandGroupingWarnings;
	// variable names declared as global (collected from globals: sections)
	std::unordered_set<std::string> declaredGlobalVariables;
	// User-facing aliases for concrete types discovered from macro replacements like @intrinsic("type", ...).
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
	// WARNING: This exists only for per-call macro expansion isolation.
	// It must NOT be used for ANYTHING else without explicit approval from the user.
	Expression *
	cloneMacroExpansionExpression(Expression *expression, bool ownRoot = true, bool preserveInferenceMetadata = false);
};

// Extract the body expression and parameter bindings from a macro PatternCall.
// If expr is a PatternCall to a macro section, returns the macro body's last expression
// and fills outBindings with parameter name → call-site argument expression.
// Returns nullptr if expr is not a macro PatternCall. Does not modify any binding stack —
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

inline Expression *
expandMacroPatternCall(ParseContext &context, Expression *expr, PatternDefinition *def, BindingMap &outBindings) {
	if (!expr || expr->kind != Expression::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return nullptr;
	auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
	if (def)
		assert(std::find(defs.begin(), defs.end(), def) != defs.end());
	(void)defs;
	if (!def || !def->section || !def->section->isMacro)
		return nullptr;
	Expression *bodyExpr = nullptr;
	for (Section *child : def->section->children) {
		for (CodeLine *line : child->codeLines) {
			if (line->expression)
				bodyExpr = line->expression;
		}
	}
	if (!bodyExpr)
		return nullptr;
	collectPatternCallBindings(expr, def, outBindings);
	Expression *expandedBody = context.cloneMacroExpansionExpression(bodyExpr);
	if (expandedBody)
		expandedBody->isExplicitGroup = true;
	return expandedBody;
}

inline Expression *expandMacroPatternCall(ParseContext &context, Expression *expr, BindingMap &outBindings) {
	if (!expr || expr->kind != Expression::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return nullptr;
	auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
	PatternDefinition *def = expr->selectedPatternDefinition;
	if (expr->selectedPatternDefinition) {
		assert(std::find(defs.begin(), defs.end(), expr->selectedPatternDefinition) != defs.end());
	} else {
		if (defs.size() != 1)
			return nullptr;
		def = defs.front();
	}
	return expandMacroPatternCall(context, expr, def, outBindings);
}
