#pragma once
#include "patternMatch.h"
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

struct ParseContext;
struct PatternReference;
struct PatternTreeNode;
namespace lsp {
struct SourceFile;
}

// Caches pointer-free matcher results across native compiler invocations.
// The key includes the trie mutation history, because a newly added overload can
// change a match even when the call never used that overload before.
class IncrementalMatcherCache {
  public:
	using Json = nlohmann::json;
	IncrementalMatcherCache(ParseContext &context, std::string_view previous);
	std::optional<PatternMatch *> reuse(PatternReference *reference, MatchOptions options, MatchDependencies *dependencies);
	void
	record(PatternReference *reference, MatchOptions options, const PatternMatch *match, const MatchDependencies *dependencies);
	std::string snapshot() const;
	size_t hits() const { return reusedCount; }

  private:
	ParseContext &context;
	std::unordered_map<std::string_view, std::string_view> previousEntries;
	std::map<std::string, std::variant<std::string, std::string_view>> recorded;
	std::unordered_map<const lsp::SourceFile *, std::string> sourceDigests;
	std::unordered_map<std::string, Json> configDigests;
	std::string treeDigest;
	std::vector<size_t> treeRevisions;
	size_t reusedCount = 0;

	bool available(MatchOptions options);
	std::string key(const PatternReference &reference, bool trackDependencies);
	std::string sourceDigest(const lsp::SourceFile *source);
	Json nodePath(const PatternTreeNode *node) const;
	PatternTreeNode *restoreNode(const Json &path) const;
	std::optional<Json> encodeMatch(const PatternReference &reference, const PatternMatch &match) const;
	PatternMatch restoreMatch(const PatternReference &reference, const Json &encoded) const;
};
