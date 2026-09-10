#include "textDocument.h"
#include "textEncoding.h"
#include <algorithm>

namespace lsp {

TextDocument::TextDocument(const std::string &uri, const std::string &content, int version)
	: SourceFile(uri, content), version(version) {}

void TextDocument::applyChange(const TextDocumentContentChangeEvent &change, int newVersion) {
	version = newVersion;

	if (!change.range) {
		// Full document replacement
		setContent(change.text);
	} else {
		// Incremental change
		size_t startOffset = positionToOffset(change.range->start);
		size_t endOffset = positionToOffset(change.range->end);

		setContent(content.substr(0, startOffset) + change.text + content.substr(endOffset));
	}
}

size_t TextDocument::positionToOffset(const Position &pos) const {
	if (pos.line < 0 || pos.line >= static_cast<int>(lineOffsets.size())) {
		return content.size();
	}

	return lineOffsets[pos.line] + byteColumn(getLine(pos.line), pos.character);
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

	int character = utf16Column(getLine(line), static_cast<int>(offset - lineOffsets[line]));

	return Position{line, character};
}

} // namespace lsp
