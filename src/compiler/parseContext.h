#pragma once
#include "codeLine.h"
#include "diagnostic.h"
#include "lsp/fileSystem.h"
#include "patternMatch.h"
#include "patternTreeNode.h"
#include "section.h"
#include <list>
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
	enum class ShaderStage { Fragment, Vertex };

	struct Options {
		std::string inputPath;
		std::string outputPath;
		bool emitLLVM = false;
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
	std::unordered_map<std::string, Type> patternParamTypes;
	// Macro expression bindings: maps variable name to Expression* (for macro expansion)
	// Only contains the CURRENT macro's parameter bindings (scoped, not inherited).
	std::unordered_map<std::string, Expression *> macroExpressionBindings;
	// Stack of caller macro bindings (pushed when entering a macro, popped when exiting).
	// Used to restore caller scope when generating resolved argument expressions.
	std::stack<std::unordered_map<std::string, Expression *>> macroBindingStack;
	// Current body section for macro expansion (used by loop intrinsics to store loop info)
	Section *currentBodySection{};
	// Current instantiation being inferred (set during non-macro function body inference)
	Instantiation *currentInstantiation{};
	// Current switch statement being built (set by "switch" intrinsic, used by "case" intrinsic)
	llvm::SwitchInst *currentSwitchInst{};
	llvm::BasicBlock *currentSwitchExitBlock{};
	// Global variables (module-level, accessible across all DynLex files in the program)
	std::unordered_map<std::string, llvm::GlobalVariable *> globalLLVMVariables;

	// Libraries required for linking (collected from @intrinsic("call", ...) calls)
	std::unordered_set<std::string> requiredLibraries;

	// String constants (maps string content to global variable)
	std::unordered_map<std::string, llvm::GlobalVariable *> stringConstants;

	// Shader uniform names (collected during codegen from @intrinsic("shader uniform", ...) calls)
	std::vector<std::string> shaderUniformNames;

	// imported source files by path (also prevents circular imports)
	std::unordered_map<std::string, lsp::SourceFile *> importedFiles;
	// The main source file (the one passed on the command line)
	lsp::SourceFile *mainSourceFile{};
	// all code lines in 'chronological' order: imported code lines get put before the import statement
	std::vector<CodeLine *> codeLines;
	std::vector<Diagnostic> diagnostics;
	Section *mainSection{};
	// for each section type, we store a tree with patterns, leading to sections.
	// we use global pattern trees which can store multiple end nodes (exclusion based).
	// this is to prevent having to search all pattern trees of every scope, or merging trees per scope.
	PatternTreeNode *patternTrees[(int)SectionType::Count];
	// Precedence level assigned to expression patterns not in the explicit precedence system.
	// Default-level patterns should not propagate minRightPrecedence constraints.
	int defaultPrecedenceLevel = 0;
	// variable references that don't correspond to any pattern element
	std::unordered_map<std::string, std::list<VariableReference *>> unresolvedVariableReferences;
	// variable names declared as global (collected from globals: sections)
	std::unordered_set<std::string> declaredGlobalVariables;
	// prohibit copies
	ParseContext(ParseContext &) = delete;
	ParseContext() {}
	void printDiagnostics();
	PatternMatch *match(PatternReference *reference);
};

// Extract the body expression and parameter bindings from a macro PatternCall.
// If expr is a PatternCall to a macro section, returns the macro body's last expression
// and fills outBindings with parameter name → call-site argument expression.
// Returns nullptr if expr is not a macro PatternCall. Does not modify any binding stack —
// the caller decides how to apply the bindings (push onto codegen stack, or pass explicitly).
inline Expression *expandMacroPatternCall(Expression *expr, std::unordered_map<std::string, Expression *> &outBindings) {
	if (!expr || expr->kind != Expression::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return nullptr;
	auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
	PatternDefinition *def = defs.empty() ? nullptr : defs[0];
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
	std::vector<Expression *> sortedArgs = sortArgumentsByPosition(expr->arguments);
	size_t argIndex = 0;
	for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
		auto paramIt = node->parameterNames.find(def);
		if (paramIt != node->parameterNames.end() && argIndex < sortedArgs.size()) {
			outBindings[paramIt->second] = sortedArgs[argIndex++];
		}
	}
	return bodyExpr;
}