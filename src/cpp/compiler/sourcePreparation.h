#pragma once
#include "parseContext.h"

struct PreparedSourceLine {
	CodeLine *line;
	std::optional<std::string_view> importPath;
	std::vector<ParseContext::SourceTokenAnnotation> tokens;
};

struct SourcePreparationWork {
	size_t filesProcessed = 0, filesReused = 0, linesReused = 0;
};

// Products precede import expansion and the transformations of the merged stream.
// Token publication stays at its original line's demand point.
struct SourcePreparationHooks {
	virtual ~SourcePreparationHooks() = default;
	virtual std::optional<std::vector<PreparedSourceLine>> reuse(lsp::SourceFile &, const SyntaxConfig &) = 0;
	virtual void record(lsp::SourceFile &, const SyntaxConfig &, const std::vector<PreparedSourceLine> &) = 0;
};
