#pragma once
#include "parseContext.h"

bool initializeTargetLayout(ParseContext &context);
bool generateCode(
	ParseContext &context, const std::function<bool(ParseContext &)> &nativeEmitter = {},
	const std::function<void(ParseContext &)> &beforeOptimization = {}
);
