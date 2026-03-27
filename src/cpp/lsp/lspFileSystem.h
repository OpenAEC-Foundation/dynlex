#pragma once
#include "fileSystem.h"
#include "textDocument.h"
#include <memory>
#include <unordered_map>

namespace lsp {

// LSP file system implementation.
// First checks if file is open in editor (returns TextDocument*),
// then falls back to local filesystem for non-open files.
// Future: could request file content from client for remote scenarios.
class LspFileSystem : public FileSystem {
  public:
	LspFileSystem(std::unordered_map<std::string, std::unique_ptr<TextDocument>> &documents) : documents(documents) {}

	SourceFile *getFile(const std::string &path) override;

  private:
	std::unordered_map<std::string, std::unique_ptr<TextDocument>> &documents;
	LocalFileSystem localFs; // fallback for files not open in editor
};

} // namespace lsp
