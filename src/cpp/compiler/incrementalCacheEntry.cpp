#include "incrementalCacheEntry.h"
#include "semanticIdentity.h"
#include <stdexcept>

std::optional<IncrementalCacheEntry> IncrementalCacheEntry::open(const std::filesystem::path &path) {
	const std::u8string utf8 = path.u8string();
	llvm::StringRef filename(reinterpret_cast<const char *>(utf8.data()), utf8.size());
	// Large payloads stay mapped and are decoded only when their stage needs them.
	auto contents = llvm::MemoryBuffer::getFile(filename, false, false);
	if (!contents) {
		if (contents.getError() == std::errc::no_such_file_or_directory)
			return std::nullopt;
		throw std::filesystem::filesystem_error("cannot read cache entry", path, contents.getError());
	}
	if ((*contents)->getBufferSize() > maximumBytes)
		throw std::runtime_error("cache entry exceeds the size limit: " + filename.str());
	return IncrementalCacheEntry(std::move(*contents));
}

std::string_view IncrementalCacheEntry::payload() const {
	std::string_view entry(contents->getBufferStart(), contents->getBufferSize());
	const size_t offset = header.size() + 65;
	if (entry.size() < offset || !entry.starts_with(header) || entry[offset - 1] != '\n' ||
		stableIdentityDigest(entry.substr(offset)) != entry.substr(header.size(), 64))
		throw std::runtime_error("corrupt cache entry: " + contents->getBufferIdentifier().str());
	return entry.substr(offset);
}
