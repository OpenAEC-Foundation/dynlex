#include "incrementalCacheEntry.h"
#include "stableIdentity.h"
#include "llvm/Support/FileSystem.h"
#ifdef _WIN32
#include "../../runtime/windowsFileDisposition.h"
#include "llvm/Support/Windows/WindowsSupport.h"
#include "llvm/Support/WindowsError.h"
#endif
#include <stdexcept>
#include <string>

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

void IncrementalCacheEntry::evict(const std::filesystem::path &path) {
	const std::u8string utf8 = path.u8string();
	const llvm::StringRef filename(reinterpret_cast<const char *>(utf8.data()), utf8.size());
	std::error_code error;
#ifdef _WIN32
	llvm::SmallVector<wchar_t, 128> nativePath;
	error = llvm::sys::windows::widenPath(filename, nativePath);
	if (error)
		throw std::filesystem::filesystem_error("cannot open cache entry for eviction", path, error);
	HANDLE file = CreateFileW(
		nativePath.data(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_FLAG_OPEN_REPARSE_POINT, nullptr
	);
	if (file == INVALID_HANDLE_VALUE) {
		error = llvm::mapWindowsError(GetLastError());
	} else {
		// Delete-on-close leaves the pathname inaccessible while a snapshot is mapped.
		// POSIX disposition unlinks the name while existing mappings retain the bytes.
		if (!dynlex_unlink_windows_file(file, 0))
			error = llvm::mapWindowsError(GetLastError());
		if (auto closeError = llvm::sys::fs::closeFile(file); !error)
			error = closeError;
	}
#else
	error = llvm::sys::fs::remove(filename);
#endif
	if (error && error != std::errc::no_such_file_or_directory)
		throw std::filesystem::filesystem_error("cannot evict cache entry", path, error);
}

IncrementalCacheEntry IncrementalCacheEntry::fromPayload(std::string_view payload) {
	if (payload.size() > maximumBytes - header.size() - 65)
		throw std::runtime_error("cache entry exceeds the size limit");
	std::string bytes;
	bytes.reserve(header.size() + 65 + payload.size());
	bytes.append(header).append(stableIdentityDigest(payload)).append("\n").append(payload);
	return IncrementalCacheEntry(llvm::MemoryBuffer::getMemBufferCopy(bytes, "produced cache entry"));
}

std::string_view IncrementalCacheEntry::payload() const {
	std::string_view entry(contents->getBufferStart(), contents->getBufferSize());
	const size_t offset = header.size() + 65;
	if (entry.size() < offset || !entry.starts_with(header) || entry[offset - 1] != '\n' ||
		stableIdentityDigest(entry.substr(offset)) != entry.substr(header.size(), 64))
		throw std::runtime_error("corrupt cache entry: " + contents->getBufferIdentifier().str());
	return entry.substr(offset);
}

bool IncrementalCacheSnapshots::observe(const std::filesystem::path &path) {
	auto [entry, inserted] = entries.try_emplace(path);
	if (inserted)
		entry->second = IncrementalCacheEntry::open(path);
	return entry->second.has_value();
}

bool IncrementalCacheSnapshots::available(const std::filesystem::path &path) const {
	return entries.at(path).has_value();
}

std::string_view IncrementalCacheSnapshots::payload(const std::filesystem::path &path) const {
	return entries.at(path).value().payload();
}

void IncrementalCacheSnapshots::retainProduced(const std::filesystem::path &path, std::string_view payload) {
	auto &entry = entries[path];
	if (!entry)
		entry = IncrementalCacheEntry::fromPayload(payload);
}
