#include "textDocument.h"
#include <algorithm>

namespace lsp {

TextDocument::TextDocument(const std::string &uri, const std::string &content, int version)
	: uri(uri), content(content), version(version) {
	rebuildLineOffsets();
}

void TextDocument::rebuildLineOffsets() {
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

void TextDocument::applyChange(const TextDocumentContentChangeEvent &change, int newVersion) {
	version = newVersion;

	if (!change.range) {
		// Full document replacement
		content = change.text;
	} else {
		// Incremental change
		size_t startOffset = positionToOffset(change.range->start);
		size_t endOffset = positionToOffset(change.range->end);

		content = content.substr(0, startOffset) + change.text + content.substr(endOffset);
	}

	rebuildLineOffsets();
}

size_t TextDocument::positionToOffset(const Position &pos) const {
	if (pos.line < 0 || pos.line >= static_cast<int>(lineOffsets.size())) {
		return content.size();
	}

	size_t lineStart = lineOffsets[pos.line];
	size_t lineEnd = (pos.line + 1 < static_cast<int>(lineOffsets.size())) ? lineOffsets[pos.line + 1] : content.size();

	// Clamp character to line length
	size_t offset = lineStart + pos.character;
	return std::min(offset, lineEnd);
}

Position TextDocument::offsetToPosition(size_t offset) const {
	if (offset >= content.size()) {
		offset = content.size();
	}

	// Binary search for the line
	auto it = std::upper_bound(lineOffsets.begin(), lineOffsets.end(), offset);
	int line = static_cast<int>(it - lineOffsets.begin()) - 1;
	if (line < 0)
		line = 0;

	int character = static_cast<int>(offset - lineOffsets[line]);

	return Position{line, character};
}

std::string_view TextDocument::getLine(int lineIndex) const {
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

} // namespace lsp
