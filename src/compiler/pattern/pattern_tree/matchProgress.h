#pragma once
#include "codeLine.h"
#include "patternMatch.h"
#include "patternTreeNode.h"
#include "sectionType.h"
struct ParseContext;
struct PatternReference;
// traversing the tree will also output a tree of possibilities
//  (which way should we search first? should we substitute a literal as variable or not?)
struct MatchProgress {
	MatchProgress(ParseContext *context, PatternReference *patternReference);
	// copy constructor, for cloning matchprogresses
	MatchProgress(const MatchProgress &other);
	MatchProgress &operator=(const MatchProgress &) = default;
	// the parent match we continue matching when this match is finished (can be promoted to grandparent)
	// we don't need child nodes, since the youngest node is always the matching once.
	MatchProgress *parent{};
	ParseContext *context{};
	// the root node of the current node
	PatternTreeNode *rootNode{};
	// the node this step is at, currently.
	PatternTreeNode *currentNode{};
	// the match result being built
	PatternMatch match{};
	PatternReference *patternReference{};

	// the pattern type we're currently matching for
	SectionType type{};

	bool isComplete() const;

	size_t sourceElementIndex{};
	size_t sourceCharIndex{};
	size_t patternStartPos{}; // where this match started in pattern
	size_t patternPos{};	  // current position in pattern
	size_t sourceArgumentIndex{};
	// returns a vector containing alternative steps we could take through the pattern tree, ordered from least important ([0])
	// to most important ([length() - 1])
	std::vector<MatchProgress> step();
	// whether this progress can start a submatch
	bool canSubstitute() const;
	// whether this progress can be a submatch
	bool canBeSubstitute() const;
	void addMatchData(PatternMatch &match);
};