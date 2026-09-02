#pragma once
#include "codeLine.h"
#include "patternMatch.h"
#include "patternTreeNode.h"
#include "sectionType.h"
#include <deque>
#include <limits>
#include <string>
#include <unordered_set>
struct ParseContext;
struct PatternReference;
struct MatchProgress;
struct MatchParentAlternatives;
struct MatchStorage;
struct MatchStep;

inline constexpr size_t noMatchSequenceNode = std::numeric_limits<size_t>::max();

template <typename T> struct MatchSequenceNode {
	T value;
	size_t previous = noMatchSequenceNode;
};

template <typename T> struct MatchSequence {
	size_t last = noMatchSequenceNode;
	size_t count = 0;

	size_t size() const { return count; }
};

struct MatchState {
	MatchSequence<PatternTreeNode *> nodesPassed;
	MatchSequence<VariableMatch> discoveredVariables;
	MatchSequence<AcceptedLiteralMatch> acceptedLiterals;
	MatchSequence<const PatternMatch *> subMatches;
	MatchSequence<MatchedArgument> orderedArguments;
};

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

void collectMatchDependencies(const MatchControlState &state, MatchDependencies &dependencies);
void normalizeMatchDependencies(MatchDependencies &dependencies);

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
	const PatternMatch *match{};
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
	MatchState match;
	PatternReference *patternReference{};
	SectionType type{};
	MatchOptions options{};
	size_t sourceElementIndex{};
	size_t sourceCharIndex{};
	size_t patternStartPos{};
	size_t patternPos{};
	size_t sourceArgumentIndex{};
	size_t matchedArgumentIndex{};

	bool isComplete(const std::vector<PatternDefinition *> &visibleDefinitions) const;
	bool isSubmatchComplete(const std::vector<PatternDefinition *> &visibleDefinitions) const;
	MatchStep step(MatchStorage &storage, const std::vector<PatternDefinition *> &visibleDefinitions);
	MatchControlState controlState() const;
	MatchContinuationState continuationState() const;
	bool canStartSubmatch() const;
	bool canBeSubmatch() const;
	std::vector<PatternDefinition *> visibleDefinitions() const;
	CompletedMatchProgress
	completedSubmatch(MatchStorage &storage, const std::vector<PatternDefinition *> &visibleDefinitions) const;
	static MatchProgress
	resumeParent(MatchStorage &storage, const MatchProgress &parentProgress, const CompletedMatchProgress &submatch);
	PatternMatch
	materializeMatch(const MatchStorage &storage, const std::vector<PatternDefinition *> &visibleDefinitions) const;
	std::string toString() const;
};

struct MatchStep {
	std::vector<MatchProgress> nextMatches;
	CompletedMatchProgress completedSubmatch;
	bool hasCompletedSubmatch = false;
};

struct MatchStorage {
	std::deque<MatchParentAlternatives> parentAlternatives;
	std::deque<MatchProgress> parentProgresses;
	std::deque<PatternMatch> completedMatches;
	std::vector<MatchSequenceNode<PatternTreeNode *>> matchedNodes;
	std::vector<MatchSequenceNode<VariableMatch>> matchedVariables;
	std::vector<MatchSequenceNode<AcceptedLiteralMatch>> acceptedLiterals;
	std::vector<MatchSequenceNode<const PatternMatch *>> subMatches;
	std::vector<MatchSequenceNode<MatchedArgument>> orderedArguments;

	MatchParentAlternatives *createParentAlternatives();
	const MatchProgress *storeParent(MatchProgress progress);
	const PatternMatch *storeCompletedMatch(PatternMatch match);
	void append(MatchSequence<PatternTreeNode *> &sequence, PatternTreeNode *value);
	void append(MatchSequence<VariableMatch> &sequence, VariableMatch value);
	void append(MatchSequence<AcceptedLiteralMatch> &sequence, AcceptedLiteralMatch value);
	void append(MatchSequence<const PatternMatch *> &sequence, const PatternMatch *value);
	void append(MatchSequence<MatchedArgument> &sequence, MatchedArgument value);
};
