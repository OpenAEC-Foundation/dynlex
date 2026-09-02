#pragma once
#include "patternTreeNode.h"
#include "variableMatch.h"
#include <cstddef>

struct MatchDependency {
	enum class Kind { Endpoint, ArgumentChild, LiteralChild };

	Kind kind;
	const PatternTreeNode *node;
	size_t endpointRevision = 0;
	std::string literal;
};

using MatchDependencies = std::vector<MatchDependency>;

struct MatchOptions {
	bool acceptLiterals = false;
	size_t maxSteps = 0; // 0 means unbounded
};

struct AcceptedLiteralMatch {
	PatternTreeNode *node{};
};

struct MatchedArgument {
	enum class Kind { Expression, SubMatch, Variable };

	size_t argumentIndex{};
	Kind kind{};
	Expression *expression{};
	size_t itemIndex{};
};

struct PatternMatch {
	PatternTreeNode *matchedEndNode;
	std::vector<PatternDefinition *> matchingDefinitions{};
	size_t lineStartPos;
	size_t lineEndPos;
	std::vector<PatternTreeNode *> nodesPassed{};
	std::vector<VariableMatch> discoveredVariables{};
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
