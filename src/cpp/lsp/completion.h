#pragma once
#include "lspProtocol.h"
#include <string>
#include <string_view>

struct ParseContext;

namespace lsp {

struct CompletionContext {
	ParseContext *parseContext{};
	std::string uri;
	std::string linePrefix;
	std::string workspaceRootPath;
	int line = 0;
	int character = 0;
};

CompletionContext makeCompletionContext(
	ParseContext *parseContext, std::string uri, std::string_view sourceLine, std::string workspaceRootPath,
	int sourceLineIndex, int sourceCharacter
);
CompletionList collectCompletions(const CompletionContext &context);
PatternFrontierList collectPatternFrontiers(const CompletionContext &context);
FilterContinuationsResult
filterPatternContinuations(const CompletionContext &context, const std::vector<std::string> &continuations);

std::string
renderCompletionDebugReport(ParseContext &context, const std::string &path, int zeroBasedLine, int zeroBasedCharacter);

} // namespace lsp
