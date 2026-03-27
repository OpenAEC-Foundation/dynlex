#include "lspFileSystem.h"

namespace lsp {

SourceFile *LspFileSystem::getFile(const std::string &path) {
	// First, check if the file is open in the editor
	// Try both the path as-is and as a file:// URI
	auto it = documents.find(path);
	if (it != documents.end()) {
		return it->second.get(); // TextDocument* is a SourceFile*
	}

	// Try with file:// prefix
	std::string uri = "file://" + path;
	it = documents.find(uri);
	if (it != documents.end()) {
		return it->second.get();
	}

	// File not open in editor - fall back to local filesystem
	// This works for local development; for remote scenarios,
	// this could be extended to request content from the client
	return localFs.getFile(path);
}

} // namespace lsp
