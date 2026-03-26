#include "fileSystem.h"
#include "fileFunctions.h"

namespace lsp {

SourceFile *LocalFileSystem::getFile(const std::string &path) {
	// Check cache first
	auto it = cache.find(path);
	if (it != cache.end()) {
		return it->second.get();
	}

	// Read from disk
	std::string content;
	if (!readStringFromFile(path, content)) {
		return nullptr;
	}

	// Cache and return
	auto [inserted, _] = cache.emplace(path, std::make_unique<SourceFile>(path, std::move(content)));
	return inserted->second.get();
}

SourceFile *MemoryFileSystem::getFile(const std::string &path) {
	auto it = files.find(path);
	if (it != files.end())
		return it->second.get();
	if (!fallback)
		return nullptr;
	return fallback->getFile(path);
}

void MemoryFileSystem::setFile(const std::string &path, std::string content) {
	auto [it, inserted] = files.emplace(path, nullptr);
	if (inserted)
		it->second = std::make_unique<SourceFile>(path, std::move(content));
	else
		it->second->content = std::move(content);
}

void MemoryFileSystem::removeFile(const std::string &path) { files.erase(path); }

void MemoryFileSystem::clear() { files.clear(); }

} // namespace lsp
