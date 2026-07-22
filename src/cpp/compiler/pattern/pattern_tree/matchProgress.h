#pragma once
#include "codeLine.h"
#include "patternMatch.h"
#include "patternTreeNode.h"
#include "sectionType.h"
#include <memory>
#include <string>
#include <unordered_set>
struct ParseContext;
struct PatternReference;
struct MatchProgress;
struct MatchParentAlternatives;
struct MatchStorage;

struct MatchControlState {
	const PatternTreeNode *rootNode{};
	const PatternTreeNode *currentNode{};
	const PatternReference *patternReference{};
	SectionType type{};
	bool acceptLiterals{};
	bool hasParents{};
	size_t sourceElementIndex{};
	size_t sourceCharIndex{};
	size_t sourceArgumentIndex{};
	size_t patternStartPos{};
	size_t patternPos{};
	size_t matchedArgumentIndex{};

	bool operator==(const MatchControlState &other) const = default;
};

struct MatchControlStateHash {
	size_t operator()(const MatchControlState &state) const;
};

struct MatchContinuationState {
	MatchControlState control;
	const MatchParentAlternatives *parents{};

	bool operator==(const MatchContinuationState &other) const = default;
};

struct MatchContinuationStateHash {
	size_t operator()(const MatchContinuationState &state) const;
};

struct CompletedMatchState {
	const PatternTreeNode *currentNode{};
	size_t sourceElementIndex{};
	size_t sourceArgumentIndex{};
	size_t patternStartPos{};
	size_t patternPos{};

	bool operator==(const CompletedMatchState &other) const = default;
};

struct CompletedMatchStateHash {
	size_t operator()(const CompletedMatchState &state) const;
};

struct CompletedMatchProgress {
	PatternTreeNode *currentNode{};
	PatternMatch match{};
	size_t sourceElementIndex{};
	size_t sourceArgumentIndex{};
	size_t patternStartPos{};
	size_t patternPos{};

	CompletedMatchState state() const;
};

struct MatchParentAlternatives {
	std::vector<const MatchProgress *> values;
	std::unordered_set<MatchContinuationState, MatchContinuationStateHash> valueStates;
	std::vector<CompletedMatchProgress> completedSubmatches;
	std::unordered_set<CompletedMatchState, CompletedMatchStateHash> completedSubmatchStates;

	bool addParent(const MatchProgress *parent);
	bool addCompletion(CompletedMatchProgress completion);
};

// Traversing the tree outputs the first deterministic tree of possibilities.
struct MatchProgress {
	MatchProgress(ParseContext *context, PatternReference *patternReference);
	MatchProgress(ParseContext *context, PatternReference *patternReference, MatchOptions options);
	MatchProgress(const MatchProgress &other) = default;
	MatchProgress(MatchProgress &&other) noexcept = default;
	MatchProgress &operator=(const MatchProgress &other) = default;
	MatchProgress &operator=(MatchProgress &&other) noexcept = default;
	~MatchProgress() = default;
	MatchParentAlternatives *parents{};
	ParseContext *context{};
	PatternTreeNode *rootNode{};
	PatternTreeNode *currentNode{};
	PatternMatch match{};
	PatternReference *patternReference{};
	SectionType type{};
	MatchOptions options{};
	size_t sourceElementIndex{};
	size_t sourceCharIndex{};
	size_t patternStartPos{};
	size_t patternPos{};
	size_t sourceArgumentIndex{};
	size_t matchedArgumentIndex{};

	bool isComplete() const;
	bool isSubmatchComplete() const;
	std::vector<MatchProgress> step(MatchStorage &storage);
	MatchControlState controlState() const;
	MatchContinuationState continuationState() const;
	bool canStartSubmatch() const;
	bool canBeSubmatch() const;
	std::vector<PatternDefinition *> visibleDefinitions() const;
	CompletedMatchProgress completedSubmatch() const;
	MatchProgress resumeParent(const MatchProgress &parentProgress) const;
	static MatchProgress resumeParent(const MatchProgress &parentProgress, const CompletedMatchProgress &submatch);
	void addMatchData(PatternMatch &match) const;
	std::string toString() const;
};

struct MatchStorage {
	std::vector<std::unique_ptr<MatchParentAlternatives>> parentAlternatives;
	std::vector<std::unique_ptr<MatchProgress>> parentProgresses;

	MatchParentAlternatives *createParentAlternatives();
	const MatchProgress *storeParent(MatchProgress progress);
};
