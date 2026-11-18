#pragma once
#include <cstddef>
#include <functional>
#include <memory>

struct ParseContext;
struct ParsedSyntaxProducts;

struct ParsedSyntaxResult {
	std::shared_ptr<const ParsedSyntaxProducts> products;
	bool success = false;
	size_t expressionsReused = 0, expressionsParsed = 0, definitionsReused = 0, temporaryReused = 0, bytes = 0;
};

// Reconstruct unresolved syntax against current source ranges. Resolution and
// inference consume ordinary current-context nodes, not cached semantic state.
ParsedSyntaxResult compileWithParsedSyntaxProducts(
	ParseContext &context, const std::shared_ptr<const ParsedSyntaxProducts> &previous, const std::function<bool()> &compile
);
