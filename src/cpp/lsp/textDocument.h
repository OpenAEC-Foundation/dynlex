#pragma once
#include "lspProtocol.h"
#include "sourceFile.h"
#include <string>
#include <vector>

namespace lsp {

// Manages a single text document's content with incremental update support
class TextDocument : public SourceFile {
  public:
	TextDocument(const std::string &uri, const std::string &content, int version);

	int version;

	// Apply an incremental change to the document
	void applyChange(const TextDocumentContentChangeEvent &change, int newVersion);

	// Convert a position (line/column) to an offset in the content
	size_t positionToOffset(const Position &pos) const;

	// Convert an offset to a position
	Position offsetToPosition(size_t offset) const;

	// Get the line at a given index (0-based)
	std::string_view getLine(int lineIndex) const;

	// Get the line including its trailing line terminator, if any.
	std::string_view getLineWithTerminator(int lineIndex) const;

	// Get the number of lines
	int lineCount() const { return static_cast<int>(lineOffsets.size()); }

  private:
	std::vector<size_t> lineOffsets; // offset of each line start

	void rebuildLineOffsets();
};

} // namespace lsp
