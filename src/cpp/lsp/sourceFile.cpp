#include "sourceFile.h"

namespace lsp {

SourceFile::SourceFile(std::string uri, std::string content) : uri(std::move(uri)) { setContent(std::move(content)); }

void SourceFile::setContent(std::string text) {
	content = std::move(text);
	lineOffsets.clear();
	lineOffsets.push_back(0);

	for (size_t i = 0; i < content.size(); ++i) {
		if (content[i] == '\n') {
			lineOffsets.push_back(i + 1);
		} else if (content[i] == '\r') {
			if (i + 1 < content.size() && content[i + 1] == '\n') {
				++i; // Skip the \n in \r\n
			}
			lineOffsets.push_back(i + 1);
		}
	}
}

std::string_view SourceFile::getLine(int lineIndex) const {
	if (lineIndex < 0 || lineIndex >= static_cast<int>(lineOffsets.size())) {
		return {};
	}

	size_t start = lineOffsets[lineIndex];
	size_t end = (lineIndex + 1 < static_cast<int>(lineOffsets.size())) ? lineOffsets[lineIndex + 1] : content.size();

	// Exclude line terminators
	while (end > start && (content[end - 1] == '\n' || content[end - 1] == '\r')) {
		--end;
	}

	return std::string_view(content).substr(start, end - start);
}

std::string_view SourceFile::getLineWithTerminator(int lineIndex) const {
	if (lineIndex < 0 || lineIndex >= static_cast<int>(lineOffsets.size())) {
		return {};
	}

	size_t start = lineOffsets[lineIndex];
	size_t end = (lineIndex + 1 < static_cast<int>(lineOffsets.size())) ? lineOffsets[lineIndex + 1] : content.size();
	return std::string_view(content).substr(start, end - start);
}

} // namespace lsp
