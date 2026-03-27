#pragma once
#include "lspProtocol.h"
#include <string>

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

CompletionList collectCompletions(const CompletionContext &context);

std::string
renderCompletionDebugReport(ParseContext &context, const std::string &path, int zeroBasedLine, int zeroBasedCharacter);

} // namespace lsp
