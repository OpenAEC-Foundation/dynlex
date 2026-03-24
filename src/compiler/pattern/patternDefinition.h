#pragma once
#include "codeLine.h"
#include "pattern_tree/patternElement.h"
#include "range.h"
#include <climits>
#include <string_view>
struct Section;
struct PatternTreeNode;
struct PatternDefinition {
	Range range;
	// the section that contains this pattern definition
	Section *section{};
	// the elements of this code lines pattern (with type constraints from {type:name} syntax)
	std::vector<DefinitionPatternElement> patternElements;
	// when resolved, this pattern has been added to the pattern tree
	bool resolved{};
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
