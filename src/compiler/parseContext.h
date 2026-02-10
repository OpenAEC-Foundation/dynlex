#pragma once
#include "codeLine.h"
#include "diagnostic.h"
#include "lsp/fileSystem.h"
#include "patternMatch.h"
#include "patternTreeNode.h"
#include "section.h"
#include <list>
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
} // namespace llvm

struct ParseContext {
	struct Options {
		std::string inputPath;
		std::string outputPath;
		bool emitLLVM = false;
		int optimizationLevel = 0; // 0-3, corresponds to -O0 through -O3
		// Maximum iterations for resolving pattern references and sections.
		// Pattern resolution is iterative: each pass resolves patterns that become unambiguous
		// when other patterns are resolved. 256 iterations is sufficient for deeply nested patterns.
		int maxResolutionIterations = 256;
	} options;

	// LLVM

	// File system for reading source files (imports)
	lsp::FileSystem *fileSystem{};

	// LLVM codegen state (initialized in codegen.cpp)
	llvm::LLVMContext *llvmContext{};
	llvm::Module *llvmModule{};
	llvm::IRBuilderBase *llvmBuilder{};

	// Temporary codegen bindings (pushed/popped during generation)
	// Pattern parameter bindings: maps variable name to LLVM value (for function parameters)
	std::unordered_map<std::string, llvm::Value *> patternBindings;
	// Pattern parameter types: maps parameter name to its type (for monomorphized functions)
	std::unordered_map<std::string, Type> patternParamTypes;
	// Macro expression bindings: maps variable name to Expression* (for macro expansion)
	std::unordered_map<std::string, Expression *> macroExpressionBindings;
	// Current body section for macro expansion (used by loop intrinsics to store loop info)
	Section *currentBodySection{};
	// Current instantiation being inferred (set during non-macro function body inference)
	Instantiation *currentInstantiation{};
	// Current switch statement being built (set by "switch" intrinsic, used by "case" intrinsic)
	llvm::SwitchInst *currentSwitchInst{};
	llvm::BasicBlock *currentSwitchExitBlock{};

	// Libraries required for linking (collected from @intrinsic("call", ...) calls)
	std::unordered_set<std::string> requiredLibraries;

	// String constants (maps string content to global variable)
	std::unordered_map<std::string, llvm::GlobalVariable *> stringConstants;

	// imported source files by path (also prevents circular imports)
	std::unordered_map<std::string, lsp::SourceFile *> importedFiles;
	// all code lines in 'chronological' order: imported code lines get put before the import statement
	std::vector<CodeLine *> codeLines;
	std::vector<Diagnostic> diagnostics;
	Section *mainSection{};
	// for each section type, we store a tree with patterns, leading to sections.
	// we use global pattern trees which can store multiple end nodes (exclusion based).
	// this is to prevent having to search all pattern trees of every scope, or merging trees per scope.
	PatternTreeNode *patternTrees[(int)SectionType::Count];
	// variable references that don't correspond to any pattern element
	std::unordered_map<std::string, std::list<VariableReference *>> unresolvedVariableReferences;
	// prohibit copies
	ParseContext(ParseContext &) = delete;
	ParseContext() {}
	void printDiagnostics();
	PatternMatch *match(PatternReference *reference);
};