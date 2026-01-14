#pragma once
#include <string_view>
#include "codeLine.h"
#include "range.h"
#include "pattern_tree/patternElement.h"
struct PatternDefinition
{
	Range range;
	// the elements of this code lines pattern
	std::vector<PatternElement> patternElements;
	PatternDefinition(Range range);
};