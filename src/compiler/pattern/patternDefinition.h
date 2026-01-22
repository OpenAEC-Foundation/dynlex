#pragma once
#include "codeLine.h"
#include "pattern_tree/patternElement.h"
#include "range.h"
#include <string_view>
struct PatternDefinition {
	Range range;
	// the elements of this code lines pattern
	std::vector<PatternElement> patternElements;
	// when resolved, this pattern has been added to the pattern tree
	bool resolved{};
	PatternDefinition(Range range);
};