#include "matchProgress.h"
#include "parseContext.h"
#include "patternReference.h"
#include <algorithm>
#include <iterator>
#include <limits>

namespace {
static void combineMatchStateHash(size_t &seed, size_t value) {
	seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

static size_t firstDefinitionStartPos(const PatternTreeNode *node) {
	if (!node)
		return std::numeric_limits<size_t>::max();
	size_t firstStartPos = std::numeric_limits<size_t>::max();
	for (const auto &[ignoredDefinition, startPos] : node->definitionStartPositions)
		firstStartPos = std::min(firstStartPos, startPos);
	return firstStartPos;
}

static std::string consumedSourcePrefix(const PatternReference *reference, size_t elementIndex, size_t charIndex) {
	if (!reference)
		return {};
	const std::vector<PatternElement> &elements = reference->patternElements;
	size_t boundedElementIndex = std::min(elementIndex, elements.size());
	std::string result;
	for (size_t i = 0; i < boundedElementIndex; i++)
		result += elements[i].text;
	if (boundedElementIndex < elements.size() && charIndex > 0) {
		const std::string &currentText = elements[boundedElementIndex].text;
		size_t prefixLength = std::min(charIndex, currentText.size());
		result += currentText.substr(0, prefixLength);
	}
	return result;
}

} // namespace

size_t MatchControlStateHash::operator()(const MatchControlState &state) const {
	size_t hash = 0;
	combineMatchStateHash(hash, std::hash<const PatternTreeNode *>{}(state.rootNode));
	combineMatchStateHash(hash, std::hash<const PatternTreeNode *>{}(state.currentNode));
	combineMatchStateHash(hash, std::hash<const PatternReference *>{}(state.patternReference));
	combineMatchStateHash(hash, static_cast<size_t>(state.type));
	combineMatchStateHash(hash, state.acceptLiterals);
	combineMatchStateHash(hash, state.hasParents);
	combineMatchStateHash(hash, state.sourceElementIndex);
	combineMatchStateHash(hash, state.sourceCharIndex);
	combineMatchStateHash(hash, state.sourceArgumentIndex);
	combineMatchStateHash(hash, state.patternStartPos);
	combineMatchStateHash(hash, state.patternPos);
	combineMatchStateHash(hash, state.matchedArgumentIndex);
	return hash;
}

size_t MatchContinuationStateHash::operator()(const MatchContinuationState &state) const {
	size_t hash = MatchControlStateHash{}(state.control);
	combineMatchStateHash(hash, std::hash<const MatchParentAlternatives *>{}(state.parents));
	return hash;
}

size_t CompletedMatchStateHash::operator()(const CompletedMatchState &state) const {
	size_t hash = 0;
	combineMatchStateHash(hash, std::hash<const PatternTreeNode *>{}(state.currentNode));
	combineMatchStateHash(hash, state.sourceElementIndex);
	combineMatchStateHash(hash, state.sourceArgumentIndex);
	combineMatchStateHash(hash, state.patternStartPos);
	combineMatchStateHash(hash, state.patternPos);
	return hash;
}

CompletedMatchState CompletedMatchProgress::state() const {
	return {currentNode, sourceElementIndex, sourceArgumentIndex, patternStartPos, patternPos};
}

bool MatchParentAlternatives::addParent(const MatchProgress *parent) {
	if (!parent || !valueStates.insert(parent->continuationState()).second)
		return false;
	values.push_back(parent);
	return true;
}

bool MatchParentAlternatives::addCompletion(CompletedMatchProgress completion) {
	if (!completedSubmatchStates.insert(completion.state()).second)
		return false;
	completedSubmatches.push_back(std::move(completion));
	return true;
}

MatchProgress::MatchProgress(ParseContext *context, PatternReference *patternReference)
	: MatchProgress(context, patternReference, {}) {}

MatchProgress::MatchProgress(ParseContext *context, PatternReference *patternReference, MatchOptions options)
	: context(context), patternReference(patternReference), type(patternReference->patternType) {
	this->options = options;
	rootNode = context->patternTrees[(int)patternReference->patternType];
	currentNode = rootNode;
}

bool MatchProgress::isComplete() const { return match.matchedEndNode != nullptr; }

bool MatchProgress::isSubmatchComplete() const {
	return parents && canBeSubmatch() && currentNode && !currentNode->matchingDefinitions.empty();
}

MatchControlState MatchProgress::controlState() const {
	return {
		rootNode,
		currentNode,
		patternReference,
		type,
		options.acceptLiterals,
		parents != nullptr,
		sourceElementIndex,
		sourceCharIndex,
		sourceArgumentIndex,
		patternStartPos,
		patternPos,
		matchedArgumentIndex,
	};
}

MatchContinuationState MatchProgress::continuationState() const { return {controlState(), parents}; }

std::vector<MatchProgress> MatchProgress::step(MatchStorage &storage) {
	std::vector<MatchProgress> nextMatches = std::vector<MatchProgress>();

	bool hasSourceElement = sourceElementIndex < patternReference->patternElements.size();
	PatternElement::Type elementType = PatternElement::Type::Count;
	std::string elementText;
	if (hasSourceElement) {
		const PatternElement &sourceElement = patternReference->patternElements[sourceElementIndex];
		elementType = sourceElement.type;
		elementText = sourceElement.text;
		if (sourceCharIndex > 0 && sourceCharIndex < elementText.size())
			elementText = elementText.substr(sourceCharIndex);
	}

	// Lowest priority first. ParseContext::match() explores queue.back(), so later pushes run earlier.
	if (hasSourceElement && elementType == PatternElement::Type::Other && elementText.size() > 1) {
		bool hasFullLiteralMatch = currentNode->literalChildren.contains(elementText);
		if (!hasFullLiteralMatch) {
			for (size_t prefixLength = 1; prefixLength < elementText.size(); prefixLength++) {
				std::string prefix = elementText.substr(0, prefixLength);
				if (!currentNode->literalChildren.contains(prefix))
					continue;
				MatchProgress splitStep = *this;
				splitStep.currentNode = currentNode->literalChildren[prefix];
				splitStep.match.nodesPassed.push_back(splitStep.currentNode);
				splitStep.sourceCharIndex += prefixLength;
				splitStep.patternPos += prefixLength;
				nextMatches.push_back(std::move(splitStep));
			}
		}
	}

	if (hasSourceElement && options.acceptLiterals && elementType == PatternElement::Type::Variable) {
		struct LiteralFallbackCandidate {
			PatternTreeNode *node{};
			size_t firstStartPos{std::numeric_limits<size_t>::max()};
		};
		std::vector<LiteralFallbackCandidate> literalFallbackChildren;
		literalFallbackChildren.reserve(currentNode->literalChildren.size());
		for (const auto &[ignoredText, child] : currentNode->literalChildren) {
			if (!child || child->type != PatternElement::Type::VariableLike)
				continue;
			literalFallbackChildren.push_back({child, firstDefinitionStartPos(child)});
		}
		std::sort(
			literalFallbackChildren.begin(), literalFallbackChildren.end(),
			[](const LiteralFallbackCandidate &left, const LiteralFallbackCandidate &right) {
			if (left.firstStartPos != right.firstStartPos)
				return left.firstStartPos < right.firstStartPos;
			return left.node->text < right.node->text;
		}
		);
		for (const LiteralFallbackCandidate &candidate : literalFallbackChildren) {
			PatternTreeNode *child = candidate.node;
			MatchProgress acceptedLiteralStep = *this;
			acceptedLiteralStep.currentNode = child;
			acceptedLiteralStep.match.nodesPassed.push_back(child);
			acceptedLiteralStep.match.acceptedLiterals.push_back({child});
			acceptedLiteralStep.sourceElementIndex++;
			if (!patternReference->expression || sourceArgumentIndex >= patternReference->expression->arguments.size())
				continue;
			acceptedLiteralStep.match.orderedArguments.push_back(
				{acceptedLiteralStep.matchedArgumentIndex, MatchedArgument::Kind::Expression,
				 patternReference->expression->arguments[sourceArgumentIndex], 0}
			);
			acceptedLiteralStep.sourceArgumentIndex++;
			acceptedLiteralStep.matchedArgumentIndex++;
			acceptedLiteralStep.patternPos += elementText.size();
			nextMatches.push_back(std::move(acceptedLiteralStep));
		}
	}

	if (!currentNode->matchingDefinitions.empty()) {
		// end node found — precedence and ordering are handled later during type inference
		if (!parents && sourceElementIndex == patternReference->patternElements.size()) {
			addMatchData(match);
		}

		if (canBeSubmatch()) {
			// Try extending as left operand of a new expression first (lower LIFO priority).
			// f.e: 'the result' in 'the result = 10', or '$ + $' in 'set $ to $ + $ dollars'
			if (canStartSubmatch() && rootNode->argumentChild) {
				MatchProgress clone = *this;
				clone.rootNode = rootNode;
				// advance past the argument slot — the completed sub-expression occupies it
				clone.currentNode = rootNode->argumentChild;
				clone.match = {};
				clone.match.nodesPassed.push_back(clone.currentNode);
				clone.matchedArgumentIndex = 0;

				clone.type = SectionType::Function;
				nextMatches.push_back(resumeParent(clone));
			}
			// Step up to parent match (higher LIFO priority — prefer returning to the
			// parent over speculatively extending into a new expression).
			if (parents) {
				for (auto parent = parents->values.rbegin(); parent != parents->values.rend(); parent++)
					nextMatches.push_back(resumeParent(**parent));
			}
		}
	}

	if (hasSourceElement) {
		// less priority: arguments
		if (currentNode->argumentChild) {

			auto pushArgumentCapture = [&]() -> void {
				if (elementType != PatternElement::Type::Variable && elementType != PatternElement::Type::VariableLike)
					return;
				MatchProgress substituteStep = *this;
				substituteStep.currentNode = currentNode->argumentChild;
				substituteStep.match.nodesPassed.push_back(substituteStep.currentNode);
				substituteStep.sourceElementIndex++;
				if (elementType == PatternElement::Type::VariableLike) {
					size_t lineStart = patternReference->pattern.getLinePos(patternPos);
					size_t lineEnd = patternReference->pattern.getLinePos(patternPos + elementText.size());
					size_t variableIndex = substituteStep.match.discoveredVariables.size();
					substituteStep.match.discoveredVariables.push_back({elementText, lineStart, lineEnd});
					substituteStep.match.orderedArguments.push_back(
						{substituteStep.matchedArgumentIndex, MatchedArgument::Kind::Variable, nullptr, variableIndex}
					);
				} else {
					substituteStep.match.orderedArguments.push_back(
						{substituteStep.matchedArgumentIndex, MatchedArgument::Kind::Expression,
						 patternReference->expression->arguments[sourceArgumentIndex], 0}
					);
					substituteStep.sourceArgumentIndex++;
				}
				substituteStep.matchedArgumentIndex++;
				substituteStep.patternPos += elementText.size();
				nextMatches.push_back(std::move(substituteStep));
			};

			auto pushSubmatch = [&]() {
				if (!canStartSubmatch())
					return;
				MatchProgress subMatch = *this;
				subMatch.currentNode = context->patternTrees[(int)SectionType::Function];
				subMatch.rootNode = subMatch.currentNode;
				subMatch.type = SectionType::Function;
				subMatch.patternStartPos = patternPos;
				subMatch.match = {};

				MatchProgress parentStep = *this;
				parentStep.currentNode = currentNode->argumentChild;
				parentStep.match.nodesPassed.push_back(parentStep.currentNode);
				subMatch.parents = storage.createParentAlternatives();
				subMatch.parents->addParent(storage.storeParent(std::move(parentStep)));
				nextMatches.push_back(std::move(subMatch));
			};

			pushArgumentCapture();
			pushSubmatch();
		}
		// word capture: matches a single VariableLike token as a string literal
		if (currentNode->wordChild && elementType == PatternElement::Type::VariableLike) {
			MatchProgress wordStep = *this;
			wordStep.currentNode = currentNode->wordChild;
			wordStep.match.nodesPassed.push_back(wordStep.currentNode);
			wordStep.sourceElementIndex++;
			size_t lineStart = patternReference->pattern.getLinePos(patternPos);
			size_t lineEnd = patternReference->pattern.getLinePos(patternPos + elementText.size());
			size_t wordIndex = wordStep.match.discoveredWords.size();
			wordStep.match.discoveredWords.push_back({elementText, lineStart, lineEnd});
			wordStep.match.orderedArguments.push_back(
				{wordStep.matchedArgumentIndex, MatchedArgument::Kind::Word, nullptr, wordIndex}
			);
			wordStep.matchedArgumentIndex++;
			wordStep.patternPos += elementText.size();
			nextMatches.push_back(std::move(wordStep));
		}
		// most priority: text match
		bool hasFullLiteralMatch =
			elementType != PatternElement::Type::Variable && currentNode->literalChildren.contains(elementText);
		if (hasFullLiteralMatch) {
			MatchProgress elemStep = *this;
			elemStep.currentNode = currentNode->literalChildren[elementText];
			elemStep.match.nodesPassed.push_back(elemStep.currentNode);
			elemStep.sourceElementIndex++;
			elemStep.sourceCharIndex = 0;
			elemStep.patternPos += elementText.size();
			nextMatches.push_back(std::move(elemStep));
		}
	}
	return nextMatches;
}

bool MatchProgress::canStartSubmatch() const {
	// prevent infinite recursion
	return type != SectionType::Function || currentNode != rootNode;
}

bool MatchProgress::canBeSubmatch() const { return type == SectionType::Function; }

MatchProgress MatchProgress::resumeParent(const MatchProgress &parentProgress) const {
	return resumeParent(parentProgress, completedSubmatch());
}

CompletedMatchProgress MatchProgress::completedSubmatch() const {
	CompletedMatchProgress completed;
	completed.currentNode = currentNode;
	completed.match = match;
	addMatchData(completed.match);
	completed.sourceElementIndex = sourceElementIndex;
	completed.sourceArgumentIndex = sourceArgumentIndex;
	completed.patternStartPos = patternStartPos;
	completed.patternPos = patternPos;
	return completed;
}

MatchProgress MatchProgress::resumeParent(const MatchProgress &parentProgress, const CompletedMatchProgress &submatch) {
	MatchProgress resumed = parentProgress;
	size_t subMatchIndex = resumed.match.subMatches.size();
	resumed.match.subMatches.push_back(submatch.match);
	resumed.match.orderedArguments.push_back(
		{resumed.matchedArgumentIndex, MatchedArgument::Kind::SubMatch, nullptr, subMatchIndex}
	);
	// We already consumed the submatch's last source element.
	resumed.sourceElementIndex = submatch.sourceElementIndex;
	resumed.sourceArgumentIndex = submatch.sourceArgumentIndex;
	resumed.matchedArgumentIndex++;
	resumed.patternPos = submatch.patternPos;
	return resumed;
}

void MatchProgress::addMatchData(PatternMatch &match) const {
	match.matchedEndNode = currentNode;
	match.lineStartPos = patternReference->pattern.getLinePos(patternStartPos);
	match.lineEndPos = patternReference->pattern.getLinePos(patternPos);
}

std::string MatchProgress::toString() const {
	const std::string currentConsumed = consumedSourcePrefix(patternReference, sourceElementIndex, sourceCharIndex);
	if (!parents || parents->values.empty())
		return currentConsumed;

	const MatchProgress &parent = *parents->values.front();
	const std::string parentRendered = parent.toString();
	const std::string parentConsumed =
		consumedSourcePrefix(parent.patternReference, parent.sourceElementIndex, parent.sourceCharIndex);
	std::string submatchConsumed = currentConsumed;
	if (currentConsumed.size() >= parentConsumed.size() &&
		currentConsumed.compare(0, parentConsumed.size(), parentConsumed) == 0)
		submatchConsumed = currentConsumed.substr(parentConsumed.size());
	return parentRendered + "(" + submatchConsumed;
}

const MatchProgress *MatchStorage::storeParent(MatchProgress progress) {
	parentProgresses.push_back(std::make_unique<MatchProgress>(std::move(progress)));
	return parentProgresses.back().get();
}

MatchParentAlternatives *MatchStorage::createParentAlternatives() {
	parentAlternatives.push_back(std::make_unique<MatchParentAlternatives>());
	return parentAlternatives.back().get();
}
