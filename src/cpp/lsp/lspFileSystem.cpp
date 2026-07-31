#include "lspFileSystem.h"

namespace lsp {

SourceFile *LspFileSystem::snapshot(const TextDocument &document) {
	auto [it, inserted] = openFileSnapshots.try_emplace(document.uri, nullptr);
	if (inserted)
		it->second = std::make_unique<SourceFile>(document.uri, document.content);
	return it->second.get();
}

SourceFile *LspFileSystem::getFile(const std::string &path) {
	auto it = documents.find(path);
	if (it != documents.end())
		return snapshot(*it->second);

	std::string uri = "file://" + path;
	it = documents.find(uri);
	if (it != documents.end())
		return snapshot(*it->second);

	return localFs.getFile(path);
}

} // namespace lsp
