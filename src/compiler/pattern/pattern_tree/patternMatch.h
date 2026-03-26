#pragma once
#include "patternTreeNode.h"
#include "variableMatch.h"
#include <cstddef>

struct MatchOptions {
	bool acceptLiterals = false;
	size_t maxSteps = 0; // 0 means unbounded
};

struct AcceptedLiteralMatch {
	PatternTreeNode *node{};
};

struct MatchedArgument {
	enum class Kind { Expression, SubMatch, Variable, Word };

	size_t argumentIndex{};
	Kind kind{};
	Expression *expression{};
	size_t itemIndex{};
};

struct PatternMatch {
	PatternTreeNode *matchedEndNode;
	size_t lineStartPos;
	size_t lineEndPos;
	std::vector<PatternTreeNode *> nodesPassed{};
	std::vector<VariableMatch> discoveredVariables{};
	std::vector<WordMatch> discoveredWords{};
	std::vector<AcceptedLiteralMatch> acceptedLiterals{};
	std::vector<PatternMatch> subMatches{};
	std::vector<MatchedArgument> orderedArguments{};

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
