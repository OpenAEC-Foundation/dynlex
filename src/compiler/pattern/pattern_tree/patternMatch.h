#pragma once
#include "patternTreeNode.h"
#include "variableMatch.h"

struct PatternMatch {
	PatternTreeNode *matchedEndNode;
	size_t lineStartPos;
	size_t lineEndPos;
	std::vector<PatternTreeNode *> nodesPassed{};
	std::vector<VariableMatch> discoveredVariables{};
	std::vector<WordMatch> discoveredWords{};
	std::vector<PatternMatch> subMatches{};
	// the arguments
	std::vector<Expression *> arguments;

	// Format signature from nodesPassed, e.g. "$ + $", "print $"
	std::string toString() const {
		std::string result;
		for (PatternTreeNode *node : nodesPassed) {
			if (node->type == PatternElement::Variable)
				result += "$";
			else
				result += node->text;
		}
		return result;
	}
};