#pragma once
#include <unordered_map>
#include "patternElement.h"

struct PatternTreeNode : public PatternElement
{
	struct Section *matchingSection{};
	//these child nodes branch off based on their pattern strings
	std::unordered_map<std::string, PatternTreeNode *> literalChildren{};
	//this child node accepts a variable or the result of an expression
	PatternTreeNode* argumentChild{};
	using PatternElement::PatternElement;
	void addPatternPart(std::vector<PatternElement> &elements, Section* matchingSection, size_t index = 0);
	PatternTreeNode* match(const std::vector<PatternElement>& elements);
};
