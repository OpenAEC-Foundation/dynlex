#pragma once
#include "codeLine.h"
#include "patternTreeNode.h"
#include "sectionType.h"
struct ParseContext;
struct PatternReference;
struct PatternMatch;
// traversing the tree will also output a tree of possibilities
//  (which way should we search first? should we substitute a literal as variable or not?)
struct MatchProgress {
	MatchProgress(ParseContext *context, PatternReference *patternReference);
	//copy constructor, for cloning matchprogresses
	MatchProgress(const MatchProgress& other);
	// the parent match we continue matching when this match is finished (can be promoted to grandparent)
	// we don't need child nodes, since the youngest node is always the matching once.
	MatchProgress *parent{};
	ParseContext *context{};
	// the pattern type we're currently matching for
	SectionType type{};
	// the root node of the current node
	PatternTreeNode *rootNode{};
	// the node this step is at, currently.
	PatternTreeNode *currentNode{};
	// the tree node which matched successfully
	PatternMatch *result{};
	PatternReference *patternReference{};

	// the nodes this progress passed already
	std::vector<PatternTreeNode *> nodesPassed{};

	int sourceElementIndex{};
	int sourceCharIndex{};
	// returns a vector containing alternative steps we could take through the pattern tree, ordered from least important ([0])
	// to most important ([length() - 1])
	std::vector<MatchProgress> step();
	// wether this progress can start a submatch
	bool canSubstitute() const;
	// wether this progress can be a submatch
	bool canBeSubstitute() const;
};