#pragma once
#include "sourceTransform.h"
#include <optional>
#include <span>
#include <vector>

struct SyntaxConfig;

// Offsets into a logical source line; no owned strings or mutable semantic state.
struct SectionLineSyntax {
	size_t patternStart = 0, patternLength = 0;
	bool opensSection = false, importDirective = false;
};

SectionLineSyntax sectionLineSyntax(const CodeLine &line, const SyntaxConfig &syntax);

struct SourceTransformWork {
	size_t chunksProcessed = 0, chunksReused = 0;
	size_t inputLinesProcessed = 0, inputLinesReused = 0, sectionLinesReused = 0, sectionLinesProcessed = 0;
};

struct SourceTransformHooks {
	// Aligned with the final merged stream, consumed by ordinary section analysis.
	std::vector<SectionLineSyntax> sectionLines;
	virtual ~SourceTransformHooks() = default;
	virtual std::optional<size_t>
	reuse(std::span<CodeLine *const> input, size_t start, SourceTransformState &state, std::vector<CodeLine *> &output) = 0;
	virtual void record(
		std::span<CodeLine *const> input, size_t start, size_t end, const SourceTransformState &before,
		const SourceTransformState &after, std::span<CodeLine *const> output, size_t tokenStart
	) = 0;
};

bool preprocessSourceLines(ParseContext &context);
