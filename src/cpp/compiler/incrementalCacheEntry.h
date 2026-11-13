#pragma once
#include "llvm/Support/MemoryBuffer.h"
#include <cstdint>
#include <filesystem>
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
	std::string_view payload() const;

  private:
	explicit IncrementalCacheEntry(std::unique_ptr<llvm::MemoryBuffer> contents) : contents(std::move(contents)) {}
	std::unique_ptr<llvm::MemoryBuffer> contents;
};
