#pragma once
#include <vector>
#include "codeLine.h"
#include "diagnostic.h"
#include "section.h"
#include "patternTreeNode.h"
#include "patternMatch.h"
struct ParseContext
{
	// settings
	int maxResolutionIterations = 0x100;
	// all code lines in 'chronological' order: imported code lines get put before the import statement
	std::vector<CodeLine *> codeLines;
	std::vector<Diagnostic> diagnostics;
	Section *mainSection{};
	// for each section type, we store a tree with patterns, leading to sections.
	// we use global pattern trees which can store multiple end nodes (exclusion based).
	// this is to prevent having to search all pattern trees of every scope, or merging trees per scope.
	PatternTreeNode *patternTrees[(int)SectionType::Count];
	// prohibit copies
	ParseContext(ParseContext &) = delete;
	ParseContext() {}
	void reportDiagnostics();
	PatternMatch *match(PatternReference *reference);
};