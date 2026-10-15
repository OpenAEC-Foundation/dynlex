#pragma once
#include "parseContext.h"

// Native command-line builds reuse the complete IR query when its filesystem
// observations are unchanged and independently cache native module partitions.
// Cache errors are explicit failures, never hidden by compiling around them.
bool compileIncrementally(
	ParseContext &context, const std::string &cacheDirectory, const std::string &compilerPath, bool showStatistics
);
