#pragma once
#include "llvm/Support/MemoryBuffer.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

namespace llvm {
class raw_fd_stream;
}

inline constexpr std::string_view parserIndexPrefix = "parser-index-";

struct CacheTrimWork {
	size_t entriesScanned = 0;
	size_t entriesSorted = 0;
	size_t entriesRemoved = 0;
	double scanMilliseconds = 0;
	double orderMilliseconds = 0;
};

// The cache size checkpoint covers immutable, atomically published entries.
// Publishers serialize their individual directory changes, not compilation.
class IncrementalCacheDirectory {
  public:
	explicit IncrementalCacheDirectory(const std::filesystem::path &directory);
	~IncrementalCacheDirectory();
	static IncrementalCacheDirectory &current();
	void publish(const std::filesystem::path &path, std::string_view payload);
	void remove(const std::filesystem::path &path);
	CacheTrimWork trim();

  private:
	std::optional<uint64_t> checkedSize();
	void updateCheckpoint(std::optional<uint64_t> bytes);
	std::filesystem::path directory;
	std::filesystem::path pending;
	std::filesystem::path controlPath;
	std::mutex publicationMutex;
	std::unique_ptr<llvm::raw_fd_stream> control;
};

// Retain the immutable file, rather than its pathname, while a build uses it.
// Atomic replacement and cache eviction cannot invalidate a loaded snapshot.
class IncrementalCacheEntry {
  public:
	static constexpr std::string_view header = "DynLex incremental cache v1\n";
	static constexpr uintmax_t maximumBytes = 256 * 1024 * 1024;
	static std::optional<IncrementalCacheEntry> open(const std::filesystem::path &path);
	static void evict(const std::filesystem::path &path);
	static IncrementalCacheEntry fromPayload(std::string_view payload);
	std::string_view payload() const;

  private:
	explicit IncrementalCacheEntry(std::unique_ptr<llvm::MemoryBuffer> contents) : contents(std::move(contents)) {}
	std::unique_ptr<llvm::MemoryBuffer> contents;
};

// A compilation observes one immutable snapshot of each content-addressed entry.
// Newly produced bytes remain owned even if eviction immediately removes their file.
class IncrementalCacheSnapshots {
  public:
	bool observe(const std::filesystem::path &path);
	bool available(const std::filesystem::path &path) const;
	std::string_view payload(const std::filesystem::path &path) const;
	void retainProduced(const std::filesystem::path &path, std::string_view payload);

  private:
	std::map<std::filesystem::path, std::optional<IncrementalCacheEntry>> entries;
};
