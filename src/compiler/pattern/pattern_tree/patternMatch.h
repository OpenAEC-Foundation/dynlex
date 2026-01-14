#pragma once
#include "patternTreeNode.h"
#include "range.h"
struct PatternMatch
{
	PatternTreeNode* matchedEndNode;
	std::string_view range;
};