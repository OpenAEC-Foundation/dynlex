#pragma once
#include "llvm/Support/MemoryBuffer.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string_view>

// Retain the immutable file, rather than its pathname, while a build uses it.
// Atomic replacement and cache eviction cannot invalidate a loaded snapshot.
class IncrementalCacheEntry {
  public:
	static constexpr std::string_view header = "DynLex incremental cache v1\n";
	static constexpr uintmax_t maximumBytes = 256 * 1024 * 1024;
	static std::optional<IncrementalCacheEntry> open(const std::filesystem::path &path);
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
