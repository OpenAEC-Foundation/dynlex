#pragma once
#include "patternElement.h"
#include <unordered_map>

struct PatternDefinition;
struct PatternTreeNode : public PatternElement {
	// the pattern definitions that end at this node (multiple when overloaded via type constraints)
	std::vector<PatternDefinition *> matchingDefinitions;
	// these child nodes branch off based on their pattern strings
	std::unordered_map<std::string, PatternTreeNode *> literalChildren{};
	// this child node accepts a variable or the result of an expression
	PatternTreeNode *argumentChild{};
	// this child node captures a single word as a string literal ({word:name} syntax)
	PatternTreeNode *wordChild{};
	// for argument/word nodes: maps pattern definition to parameter name
	// (multiple definitions can share the same argument node with different parameter names)
	std::unordered_map<PatternDefinition *, std::string> parameterNames{};
	// maps a definition to the pattern-token start offset represented by this node for that definition
	std::unordered_map<PatternDefinition *, size_t> definitionStartPositions{};
	using PatternElement::PatternElement;
	// Add a definition to the tree and record the endpoint nodes on the definition itself.
	void addPatternPart(std::vector<DefinitionPatternElement> &elements, PatternDefinition *definition);
	// Remove a definition from the tree (clears matchingDefinition and parameterNames).
	// Must be called with the SAME elements that were used in addPatternPart (before any element type changes).
	void removePatternPart(std::vector<DefinitionPatternElement> &elements, PatternDefinition *definition);
	// Find definitions already in the tree that are less specific than the given definition.
	// A definition is less specific if it has an argument/word slot where the new definition has a literal/word.
	std::vector<PatternDefinition *> findLessSpecificDefinitions(std::vector<DefinitionPatternElement> &elements);
};
