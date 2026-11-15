#pragma once
#include "codeLine.h"
#include "pattern/patternReference.h"
#include <algorithm>
#include <string_view>
#include <tuple>

inline void orderSourceReferences(std::vector<PatternReference *> &references) {
	std::ranges::sort(references, {}, [](const PatternReference *reference) {
		const Range &range = reference->range();
		return std::tuple{
			range.line->mergedLineIndex,
			range.start(),
			range.end(),
			static_cast<int>(reference->patternType),
			static_cast<int>(reference->purpose),
			std::string_view(reference->pattern.text)
		};
	});
}
