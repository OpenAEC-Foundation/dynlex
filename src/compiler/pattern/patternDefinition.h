#pragma once
#include "codeLine.h"
#include "pattern_tree/patternElement.h"
#include "range.h"
#include <climits>
#include <string_view>
struct Section;
struct PatternDefinition {
	Range range;
	// the section that contains this pattern definition
	Section *section{};
	// the elements of this code lines pattern (with type constraints from {type:name} syntax)
	std::vector<DefinitionPatternElement> patternElements;
	// when resolved, this pattern has been added to the pattern tree
	bool resolved{};
	// precedence level (higher = evaluated first). 0 = no precedence declared.
	int precedence = 0;
	PatternDefinition(Range range, Section *section);
};
