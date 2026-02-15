#pragma once
#include "patternElement.h"
#include <unordered_map>

struct PatternDefinition;
struct PatternTreeNode : public PatternElement {
	// the pattern definition that ends at this node (if any)
	PatternDefinition *matchingDefinition{};
	// these child nodes branch off based on their pattern strings
	std::unordered_map<std::string, PatternTreeNode *> literalChildren{};
	// this child node accepts a variable or the result of an expression
	PatternTreeNode *argumentChild{};
	// this child node captures a single word as a string literal ({word:name} syntax)
	PatternTreeNode *wordChild{};
	// for argument/word nodes: maps pattern definition to parameter name
	// (multiple definitions can share the same argument node with different parameter names)
	std::unordered_map<PatternDefinition *, std::string> parameterNames{};
	using PatternElement::PatternElement;
	void addPatternPart(std::vector<PatternElement> &elements, PatternDefinition *definition, size_t index = 0);
	// Remove a definition from the tree (clears matchingDefinition and parameterNames).
	// Must be called with the SAME elements that were used in addPatternPart (before any element type changes).
	void removePatternPart(std::vector<PatternElement> &elements, PatternDefinition *definition);
	PatternTreeNode *match(const std::vector<PatternElement> &elements);
	// Find definitions already in the tree that are less specific than the given definition.
	// A definition is less specific if it has an argument/word slot where the new definition has a literal/word.
	std::vector<PatternDefinition *> findLessSpecificDefinitions(std::vector<PatternElement> &elements);
};
