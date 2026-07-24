#pragma once
#include "codeLine.h"
#include "pattern_tree/patternElement.h"
#include "range.h"
#include "sectionType.h"
#include <climits>
#include <functional>
#include <string_view>
struct Section;
struct Instantiation;
struct PatternTreeNode;
struct ParseContext;
namespace lsp {
struct SourceFile;
}
struct PatternDefinition {
	Range range;
	// the section that contains this pattern definition
	Section *section{};
	Instantiation *callableInstantiation{};
	// the elements of this code lines pattern (with type constraints from {type:name} syntax)
	std::vector<DefinitionPatternElement> patternElements;
	// Compiler-generated definitions can carry constraints that directly reference
	// compiler objects and therefore cannot be reconstructed from source text.
	bool hasPrebuiltPatternElements = false;
	// Class member access is an atomic generated operation. It binds before every
	// pattern that participates in the source-declared precedence graph.
	bool isGeneratedClassPropertyAccessor = false;
	// when resolved, this pattern has been added to the pattern tree
	bool resolved{};
	// The tree and exact canonical element paths used for the current trie
	// insertion. Pattern elements can be reclassified during resolution, so
	// removal must never reconstruct old paths from their mutable state.
	PatternTreeNode *indexedTree{};
	SectionType indexedTreeType = SectionType::Count;
	std::vector<std::vector<PatternElement>> indexedPaths;
	std::vector<std::vector<PatternTreeNode *>> indexedNodePaths;
	// the exact trie endpoint nodes this definition currently ends at
	std::vector<PatternTreeNode *> endNodes;
	// precedence level (higher = evaluated first). 0 = no precedence declared.
	int precedence = 0;
	PatternDefinition(Range range, Section *section);

	std::string toString() const {
		std::string result;
		appendElements(result, patternElements);
		return result;
	}

  private:
	static void appendElements(std::string &result, const std::vector<DefinitionPatternElement> &elements) {
		for (auto &elem : elements) {
			switch (elem.type) {
			case PatternElement::Choice:
				if (!elem.alternatives.empty())
					appendElements(result, elem.alternatives[0]);
				break;
			case PatternElement::Variable:
				result += elem.text;
				break;
			default:
				result += elem.text;
				break;
			}
		}
	}
};

bool isPatternDefinitionVisibleFromSource(const PatternDefinition &definition, const lsp::SourceFile &sourceFile);
bool patternDefinitionsShareVisibilityScope(const PatternDefinition &left, const PatternDefinition &right);
void mutatePatternDefinition(ParseContext &context, PatternDefinition &definition, const std::function<void()> &mutation);
void promoteImplicitPatternParameter(
	ParseContext &context, PatternDefinition &definition, DefinitionPatternElement &element, const Range &useRange
);
void revertImplicitPatternParameter(ParseContext &context, PatternDefinition &definition, DefinitionPatternElement &element);
