#pragma once
#include "codeLine.h"
#include "diagnostic.h"
#include "patternMatch.h"
#include "patternTreeNode.h"
#include "section.h"
#include <list>
#include <unordered_map>
#include <vector>

namespace llvm {
class LLVMContext;
class Module;
class IRBuilderBase;
class AllocaInst;
class Value;
} // namespace llvm

struct ParseContext {
	struct Options {
		std::string inputPath;
		std::string outputPath;
		bool emitLLVM = false;
		int maxResolutionIterations = 0x100;
	} options;

	// LLVM codegen state (initialized in codegen.cpp)
	llvm::LLVMContext *llvmContext{};
	llvm::Module *llvmModule{};
	llvm::IRBuilderBase *llvmBuilder{};
	std::unordered_map<std::string, llvm::AllocaInst *> llvmVariables;

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
	void reportDiagnostics();
	PatternMatch *match(PatternReference *reference);
};