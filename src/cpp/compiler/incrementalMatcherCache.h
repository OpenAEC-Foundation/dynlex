#pragma once
#include "patternMatch.h"
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

struct ParseContext;
struct PatternReference;
struct PatternTreeNode;
// Caches pointer-free matcher results across native compiler invocations.
// The key includes active candidate signatures, because a newly added overload can
// change a match even when the call never used that overload before.
class IncrementalMatcherCache {
  public:
	using Json = nlohmann::json;
	IncrementalMatcherCache(ParseContext &context, std::string_view previous);
	std::optional<PatternMatch *> reuse(PatternReference *reference, MatchOptions options, MatchDependencies *dependencies);
	void
	record(PatternReference *reference, MatchOptions options, const PatternMatch *match, const MatchDependencies *dependencies);
	std::string snapshot() const;
	bool contains(std::string_view key) const;
	void retain(std::string_view key);
	struct BodyResolution {
		size_t referenceIndex;
		std::string matchKey;
		std::optional<std::tuple<PatternElement, int, int>> variable;
	};
	std::optional<std::string> recordBodyResolution(const std::vector<std::pair<size_t, PatternReference *>> &references);
	std::optional<std::vector<BodyResolution>> restoreBodyResolution(std::string_view key);
	struct ReferenceMatchKeys {
		std::string_view pattern;
		int patternType;
		int purpose;
		std::string_view finalKey;
		std::string_view initialKey;
	};
	std::string recordReferenceKeys(const std::vector<PatternReference *> &references);
	std::optional<std::vector<ReferenceMatchKeys>> restoreReferenceKeys(std::string_view key);
	std::string recordMatchIdentities(const std::vector<std::string_view> &identities);
	std::optional<std::vector<std::string_view>> restoreMatchIdentities(std::string_view key);
	std::optional<std::string_view> resultKey(const PatternReference *reference) const;
	size_t hits() const { return reusedCount; }
	size_t precedenceHits() const { return reusedPrecedenceCount; }
	size_t deferredKeyReplays() const { return prevalidatedCount - prevalidatedPrecedenceCount; }
	size_t precedenceKeyReplays() const { return prevalidatedPrecedenceCount; }
	std::string precedenceSignature() { return precedenceDigest(); }
	void reusePrecedenceSignature(std::string_view signature);
	double keyTimeMilliseconds() const { return keyMilliseconds; }
	double restoreTimeMilliseconds() const { return restoreMilliseconds; }
	size_t keyComputations() const { return keyComputationCount; }
	size_t signatureLookups() const { return signatureLookupCount; }

  private:
	ParseContext &context;
	std::unordered_map<std::string_view, std::string_view> previousEntries;
	std::map<std::string, std::variant<std::string, std::string_view>> recorded;
	std::uint64_t cachedPrecedenceRevision = 0;
	std::string cachedPrecedenceDigest;
	size_t reusedCount = 0;
	size_t reusedPrecedenceCount = 0;
	size_t prevalidatedCount = 0;
	size_t prevalidatedPrecedenceCount = 0;
	double keyMilliseconds = 0;
	double restoreMilliseconds = 0;
	size_t keyComputationCount = 0;
	size_t signatureLookupCount = 0;

	bool available(MatchOptions options);
	std::string key(PatternReference &reference, bool trackDependencies);
	std::string precedenceDigest();
	void appendNode(std::string &output, const PatternTreeNode *node) const;
	PatternTreeNode *restoreNode(std::string_view input, size_t &position) const;
	bool appendMatch(std::string &output, const PatternReference &reference, const PatternMatch &match) const;
	PatternMatch restoreMatch(std::string_view input, size_t &position, const PatternReference &reference) const;
};
