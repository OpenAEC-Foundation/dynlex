#pragma once
#include "fileSystem.h"
#include "textDocument.h"
#include <memory>
#include <unordered_map>

namespace lsp {

// Open editor documents are copied into immutable snapshots owned for this
// compilation context. Files which are not open are owned by the local cache.
class LspFileSystem : public FileSystem {
  public:
	LspFileSystem(std::unordered_map<std::string, std::unique_ptr<TextDocument>> &documents) : documents(documents) {}

	SourceFile *getFile(const std::string &path) override;

  private:
	std::unordered_map<std::string, std::unique_ptr<TextDocument>> &documents;
	std::unordered_map<std::string, std::unique_ptr<SourceFile>> openFileSnapshots;
	LocalFileSystem localFs;

	SourceFile *snapshot(const TextDocument &document);
};

} // namespace lsp
