#pragma once
#include "patternTreeNode.h"
#include "variableMatch.h"

struct PatternMatch {
	PatternTreeNode *matchedEndNode;
	size_t lineStartPos;
	size_t lineEndPos;
	std::vector<PatternTreeNode *> nodesPassed{};
	std::vector<VariableMatch> variableMatches{};
	std::vector<PatternMatch> subMatches{};
};