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

	// Convert an LSP UTF-16 position (line/column) to an offset in the content
	size_t positionToOffset(const Position &pos) const;

	// Convert an offset to a position
	Position offsetToPosition(size_t offset) const;
};

} // namespace lsp
