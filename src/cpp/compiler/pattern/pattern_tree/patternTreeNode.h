#pragma once
#include "patternElement.h"
#include "sectionType.h"
#include <memory>
#include <string_view>
#include <unordered_map>

struct PatternDefinition;
struct PatternLiteralHash {
	using is_transparent = void; // NOLINT(readability-identifier-naming)

	size_t operator()(std::string_view value) const noexcept { return std::hash<std::string_view>{}(value); }
};

struct PatternDefinitionOccurrence {
	size_t startPos;
	std::string parameterName;

	bool operator==(const PatternDefinitionOccurrence &) const = default;
};

struct PatternTreeSignatureNode {
	std::unordered_map<std::string, std::unique_ptr<PatternTreeSignatureNode>, PatternLiteralHash, std::equal_to<>> children;
	std::string digest;
};

struct PatternTreeMutationState {
	std::string definitionDigest;
	PatternTreeSignatureNode signatures;
};

struct PatternTreeNode : public PatternElement {
	// Only incremental root nodes allocate mutation state. Child nodes are never removed.
	std::unique_ptr<PatternTreeMutationState> mutationState;
	PatternTreeNode *parentNode = nullptr;
	// Incremented whenever active definitions at this endpoint change.
	size_t endpointRevision = 0;
	// the pattern definitions that end at this node (multiple when overloaded via type constraints)
	std::vector<PatternDefinition *> matchingDefinitions;
	// these child nodes branch off based on their pattern strings
	std::unordered_map<std::string, PatternTreeNode *, PatternLiteralHash, std::equal_to<>> literalChildren{};
	// this child node accepts a variable or the result of an expression
	PatternTreeNode *argumentChild{};
	PatternTreeNode *argumentPackChild{};
	// Every canonical-path occurrence that reaches this node. A definition can
	// reach the same trie node more than once through converging choice
	// alternatives, so source metadata is necessarily one-to-many.
	std::unordered_map<PatternDefinition *, std::vector<PatternDefinitionOccurrence>> definitionOccurrences{};
	using PatternElement::PatternElement;
	explicit PatternTreeNode(const PatternElement &element) : PatternElement(element) {}
	// Add a definition to the tree and record the endpoint nodes on the definition itself.
	void addPatternDefinition(PatternDefinition *definition, SectionType treeType);
	// Remove a definition through its immutable indexed-path snapshot.
	void removePatternDefinition(PatternDefinition *definition);
	// Assert that the definition snapshot, endpoints, and all trie metadata agree.
	void requirePatternDefinitionIndexed(const PatternDefinition *definition) const;
	// Find definitions already in the tree that are less specific than an indexed definition.
	// A definition is less specific if it has an argument slot where the new definition has a literal.
	std::vector<PatternDefinition *> findLessSpecificDefinitions(const PatternDefinition &definition);
};
