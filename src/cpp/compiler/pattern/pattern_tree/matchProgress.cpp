#include "matchProgress.h"
#include "parseContext.h"
#include "patternReference.h"
#include <algorithm>
#include <iterator>
#include <limits>

namespace {
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

MatchProgress::MatchProgress(ParseContext *context, PatternReference *patternReference)
	: MatchProgress(context, patternReference, {}) {}

MatchProgress::MatchProgress(ParseContext *context, PatternReference *patternReference, MatchOptions options)
	: context(context), patternReference(patternReference), type(patternReference->patternType) {
	this->options = options;
	rootNode = context->patternTrees[(int)patternReference->patternType];
	currentNode = rootNode;
}

bool MatchProgress::isComplete() const { return match.matchedEndNode != nullptr; }

std::vector<MatchProgress> MatchProgress::step() {
	std::vector<MatchProgress> nextMatches = std::vector<MatchProgress>();
	std::vector<MatchProgress> splitFallbackMatches;
	std::vector<MatchProgress> acceptedLiteralMatches;

	// submatch and use the result as argument for the parent progress
	auto stepUp = [&nextMatches, this](const MatchProgress &parentProgress) {
		// this submatch finished, so we convert it to a PatternMatch and add it to the parent progress
		MatchProgress stepUp = parentProgress;
		PatternMatch subMatch = match;
		addMatchData(subMatch);
		size_t subMatchIndex = stepUp.match.subMatches.size();
		stepUp.match.subMatches.push_back(std::move(subMatch));
		stepUp.match.orderedArguments.push_back(
			{stepUp.matchedArgumentIndex, MatchedArgument::Kind::SubMatch, nullptr, subMatchIndex}
		);
		// sourceElementIndex stays the same when stepping up, we are already past the last node
		// (we have compared the last element already, the sourceElementIndex was increased then)
		stepUp.sourceElementIndex = sourceElementIndex;
		stepUp.sourceArgumentIndex = sourceArgumentIndex;
		stepUp.matchedArgumentIndex++;
		stepUp.patternPos = patternPos;
		nextMatches.push_back(std::move(stepUp));
	};

	if (!currentNode->matchingDefinitions.empty()) {
		// end node found — precedence and ordering are handled later during type inference
		if (!parent && sourceElementIndex == patternReference->patternElements.size()) {
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
				stepUp(clone);
			}
			// Step up to parent match (higher LIFO priority — prefer returning to the
			// parent over speculatively extending into a new expression).
			if (parent) {
				stepUp(*parent);
			}
		}
	}

	if (sourceElementIndex < patternReference->patternElements.size()) {
		PatternElement elementToCompare = patternReference->patternElements[sourceElementIndex];
		if (sourceCharIndex > 0 && sourceCharIndex < elementToCompare.text.size())
			elementToCompare.text = elementToCompare.text.substr(sourceCharIndex);

		// less priority: arguments
		if (currentNode->argumentChild) {

			auto pushArgumentCapture = [&]() -> void {
				if (elementToCompare.type != PatternElement::Type::Variable &&
					elementToCompare.type != PatternElement::Type::VariableLike)
					return;
				MatchProgress substituteStep = *this;
				substituteStep.currentNode = currentNode->argumentChild;
				substituteStep.match.nodesPassed.push_back(substituteStep.currentNode);
				substituteStep.sourceElementIndex++;
				if (elementToCompare.type == PatternElement::Type::VariableLike) {
					size_t lineStart = patternReference->pattern.getLinePos(patternPos);
					size_t lineEnd = patternReference->pattern.getLinePos(patternPos + elementToCompare.text.size());
					size_t variableIndex = substituteStep.match.discoveredVariables.size();
					substituteStep.match.discoveredVariables.push_back({elementToCompare.text, lineStart, lineEnd});
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
				substituteStep.patternPos += elementToCompare.text.size();
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
				subMatch.parent = std::make_shared<MatchProgress>(std::move(parentStep));
				nextMatches.push_back(std::move(subMatch));
			};

			pushArgumentCapture();
			pushSubmatch();
		}
		if (options.acceptLiterals && elementToCompare.type == PatternElement::Type::Variable) {
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
				acceptedLiteralStep.patternPos += elementToCompare.text.size();
				acceptedLiteralMatches.push_back(std::move(acceptedLiteralStep));
			}
		}
		// word capture: matches a single VariableLike token as a string literal
		if (currentNode->wordChild && elementToCompare.type == PatternElement::Type::VariableLike) {
			MatchProgress wordStep = *this;
			wordStep.currentNode = currentNode->wordChild;
			wordStep.match.nodesPassed.push_back(wordStep.currentNode);
			wordStep.sourceElementIndex++;
			size_t lineStart = patternReference->pattern.getLinePos(patternPos);
			size_t lineEnd = patternReference->pattern.getLinePos(patternPos + elementToCompare.text.size());
			size_t wordIndex = wordStep.match.discoveredWords.size();
			wordStep.match.discoveredWords.push_back({elementToCompare.text, lineStart, lineEnd});
			wordStep.match.orderedArguments.push_back(
				{wordStep.matchedArgumentIndex, MatchedArgument::Kind::Word, nullptr, wordIndex}
			);
			wordStep.matchedArgumentIndex++;
			wordStep.patternPos += elementToCompare.text.size();
			nextMatches.push_back(std::move(wordStep));
		}
		// most priority: text match
		bool hasFullLiteralMatch = elementToCompare.type != PatternElement::Type::Variable &&
								   currentNode->literalChildren.contains(elementToCompare.text);
		if (hasFullLiteralMatch) {
			MatchProgress elemStep = *this;
			elemStep.currentNode = currentNode->literalChildren[elementToCompare.text];
			elemStep.match.nodesPassed.push_back(elemStep.currentNode);
			elemStep.sourceElementIndex++;
			elemStep.sourceCharIndex = 0;
			elemStep.patternPos += elementToCompare.text.size();
			nextMatches.push_back(std::move(elemStep));
		}

		// Lowest-priority fallback: if a full literal did not match, split only Other
		// elements into prefix+suffix and try matching the prefix as a literal token.
		if (!hasFullLiteralMatch && elementToCompare.type == PatternElement::Type::Other && elementToCompare.text.size() > 1) {
			for (size_t prefixLength = 1; prefixLength < elementToCompare.text.size(); prefixLength++) {
				std::string prefix = elementToCompare.text.substr(0, prefixLength);
				if (!currentNode->literalChildren.contains(prefix))
					continue;
				MatchProgress splitStep = *this;
				splitStep.currentNode = currentNode->literalChildren[prefix];
				splitStep.match.nodesPassed.push_back(splitStep.currentNode);
				splitStep.sourceCharIndex += prefixLength;
				splitStep.patternPos += prefixLength;
				splitFallbackMatches.push_back(std::move(splitStep));
			}
		}
	}
	if (!acceptedLiteralMatches.empty())
		nextMatches.insert(
			nextMatches.begin(), std::make_move_iterator(acceptedLiteralMatches.begin()),
			std::make_move_iterator(acceptedLiteralMatches.end())
		);
	if (!splitFallbackMatches.empty())
		nextMatches.insert(
			nextMatches.begin(), std::make_move_iterator(splitFallbackMatches.begin()),
			std::make_move_iterator(splitFallbackMatches.end())
		);
	return nextMatches;
}

bool MatchProgress::canStartSubmatch() const {
	// prevent infinite recursion
	return type != SectionType::Function || currentNode != rootNode;
}

bool MatchProgress::canBeSubmatch() const { return type == SectionType::Function; }

void MatchProgress::addMatchData(PatternMatch &match) {
	match.matchedEndNode = currentNode;
	match.lineStartPos = patternReference->pattern.getLinePos(patternStartPos);
	match.lineEndPos = patternReference->pattern.getLinePos(patternPos);
}

std::string MatchProgress::toString() const {
	const std::string currentConsumed = consumedSourcePrefix(patternReference, sourceElementIndex, sourceCharIndex);
	if (!parent)
		return currentConsumed;

	const std::string parentRendered = parent->toString();
	const std::string parentConsumed =
		consumedSourcePrefix(parent->patternReference, parent->sourceElementIndex, parent->sourceCharIndex);
	std::string submatchConsumed = currentConsumed;
	if (currentConsumed.size() >= parentConsumed.size() &&
		currentConsumed.compare(0, parentConsumed.size(), parentConsumed) == 0)
		submatchConsumed = currentConsumed.substr(parentConsumed.size());
	return parentRendered + "(" + submatchConsumed;
}
