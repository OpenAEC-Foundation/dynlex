#include "codeLine.h"
#include "completion.h"
#include "parseContext.h"
#include "pathUtils.h"
#include "sourceFile.h"
#include <algorithm>
#include <utility>

namespace lsp {

CompletionContext makeCompletionContext(
	ParseContext *parseContext, std::string uri, std::string_view sourceLine, std::string workspaceRootPath,
	int sourceLineIndex, int sourceCharacter
) {
	CompletionContext result{
		.parseContext = parseContext,
		.uri = std::move(uri),
		.linePrefix = std::string(sourceLine.substr(0, sourceCharacter)),
		.workspaceRootPath = std::move(workspaceRootPath),
		.line = sourceLineIndex,
		.character = sourceCharacter,
	};
	if (!parseContext)
		return result;

	const std::string requestedUri = pathutil::toAbsoluteUri(result.uri);
	CodeLine *identityLine = nullptr;
	CodeLine *transformedLine = nullptr;
	const SourceSlice *activeSlice = nullptr;
	for (CodeLine *line : parseContext->codeLines) {
		if (!line)
			continue;
		if (line->hasIdentitySourceMapping()) {
			if (!identityLine && line->sourceFile && line->sourceFileLineIndex == sourceLineIndex &&
				pathutil::toAbsoluteUri(line->sourceFile->uri) == requestedUri)
				identityLine = line;
			continue;
		}
		for (const SourceSlice &slice : line->sourceSlices) {
			if (slice.transformedStart == slice.transformedEnd || !slice.sourceFile ||
				slice.sourceFileLineIndex != sourceLineIndex || slice.sourceColumnStart > sourceCharacter ||
				pathutil::toAbsoluteUri(slice.sourceFile->uri) != requestedUri)
				continue;
			if (!activeSlice || slice.sourceColumnStart > activeSlice->sourceColumnStart) {
				transformedLine = line;
				activeSlice = &slice;
			}
		}
	}

	result.logicalLine = transformedLine ? transformedLine : identityLine;
	if (!transformedLine)
		return result;
	size_t sourceStart = std::min<size_t>(activeSlice->sourceColumnStart, sourceLine.size());
	size_t sourceEnd = std::min<size_t>(sourceCharacter, sourceLine.size());
	result.linePrefix = transformedLine->fullText.substr(0, activeSlice->transformedStart);
	result.linePrefix.append(sourceLine.substr(sourceStart, sourceEnd - sourceStart));
	return result;
}

} // namespace lsp
