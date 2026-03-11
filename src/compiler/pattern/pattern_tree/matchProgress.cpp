#include "matchProgress.h"
#include "parseContext.h"
#include "patternReference.h"
#include <algorithm>

MatchProgress::MatchProgress(ParseContext *context, PatternReference *patternReference)
	: context(context), patternReference(patternReference), type(patternReference->patternType) {

	rootNode = context->patternTrees[(int)patternReference->patternType];
	currentNode = rootNode;
}

MatchProgress::MatchProgress(const MatchProgress &other) {
	context = other.context;
	rootNode = other.rootNode;
	currentNode = other.currentNode;
	match = other.match;
	patternReference = other.patternReference;
	type = other.type;
	sourceElementIndex = other.sourceElementIndex;
	sourceCharIndex = other.sourceCharIndex;
	patternStartPos = other.patternStartPos;
	patternPos = other.patternPos;
	sourceArgumentIndex = other.sourceArgumentIndex;
	parent = other.parent ? new MatchProgress(*other.parent) : nullptr;
}

MatchProgress::MatchProgress(MatchProgress &&other) noexcept
	: parent(other.parent), context(other.context), rootNode(other.rootNode), currentNode(other.currentNode),
	  match(std::move(other.match)), patternReference(other.patternReference), type(other.type),
	  sourceElementIndex(other.sourceElementIndex), sourceCharIndex(other.sourceCharIndex),
	  patternStartPos(other.patternStartPos), patternPos(other.patternPos), sourceArgumentIndex(other.sourceArgumentIndex) {
	other.parent = nullptr;
}

MatchProgress &MatchProgress::operator=(const MatchProgress &other) {
	if (this == &other)
		return *this;

	delete parent;
	parent = other.parent ? new MatchProgress(*other.parent) : nullptr;
	context = other.context;
	rootNode = other.rootNode;
	currentNode = other.currentNode;
	match = other.match;
	patternReference = other.patternReference;
	type = other.type;
	sourceElementIndex = other.sourceElementIndex;
	sourceCharIndex = other.sourceCharIndex;
	patternStartPos = other.patternStartPos;
	patternPos = other.patternPos;
	sourceArgumentIndex = other.sourceArgumentIndex;
	return *this;
}

MatchProgress &MatchProgress::operator=(MatchProgress &&other) noexcept {
	if (this == &other)
		return *this;

	delete parent;
	parent = other.parent;
	other.parent = nullptr;
	context = other.context;
	rootNode = other.rootNode;
	currentNode = other.currentNode;
	match = std::move(other.match);
	patternReference = other.patternReference;
	type = other.type;
	sourceElementIndex = other.sourceElementIndex;
	sourceCharIndex = other.sourceCharIndex;
	patternStartPos = other.patternStartPos;
	patternPos = other.patternPos;
	sourceArgumentIndex = other.sourceArgumentIndex;
	return *this;
}

MatchProgress::~MatchProgress() {
	delete parent;
	parent = nullptr;
}

bool MatchProgress::isComplete() const { return match.matchedEndNode != nullptr; }

std::vector<MatchProgress> MatchProgress::step() {
	std::vector<MatchProgress> nextMatches = std::vector<MatchProgress>();

	// submatch and use the result as argument for the parent progress
	auto stepUp = [&nextMatches, this](MatchProgress &parentProgress) {
		// this submatch finished, so we convert it to a PatternMatch and add it to the parent progress
		MatchProgress stepUp = parentProgress;
		PatternMatch subMatch = match;
		addMatchData(subMatch);
		stepUp.match.subMatches.push_back(subMatch);
		// sourceElementIndex stays the same when stepping up, we are already past the last node
		// (we have compared the last element already, the sourceElementIndex was increased then)
		stepUp.sourceElementIndex = sourceElementIndex;
		stepUp.sourceArgumentIndex = sourceArgumentIndex;
		stepUp.patternPos = patternPos;
		nextMatches.push_back(stepUp);
	};

	if (!currentNode->matchingDefinitions.empty()) {
		// end node found — precedence and ordering are handled later during type inference
		if (!parent && sourceElementIndex == patternReference->patternElements.size()) {
			addMatchData(match);
		}

		if (canBeSubmatch()) {
			// Try extending as left operand of a new function first (lower LIFO priority).
			// f.e: 'the result' in 'the result = 10', or '$ + $' in 'set $ to $ + $ dollars'
			if (canStartSubmatch() && rootNode->argumentChild) {
				MatchProgress clone = *this;
				clone.rootNode = rootNode;
				// advance past the argument slot — the completed sub-function occupies it
				clone.currentNode = rootNode->argumentChild;
				clone.match = {};
				clone.match.nodesPassed.push_back(clone.currentNode);

				clone.type = SectionType::Function;
				stepUp(clone);
			}
			// Step up to parent match (higher LIFO priority — prefer returning to the
			// parent over speculatively extending into a new function).
			if (parent) {
				stepUp(*parent);
			}
		}
	}
	if (sourceElementIndex < patternReference->patternElements.size()) {
		PatternElement elementToCompare = patternReference->patternElements[sourceElementIndex];

		// less priority: arguments
		if (currentNode->argumentChild) {
			bool preferFunctionSubmatch =
				elementToCompare.type == PatternElement::Type::VariableLike &&
				context->patternTrees[(int)SectionType::Function] &&
				context->patternTrees[(int)SectionType::Function]->literalChildren.contains(elementToCompare.text);

			auto pushArgumentCapture = [&]() -> bool {
				if (elementToCompare.type == PatternElement::Type::Other)
					return true;
				MatchProgress substituteStep = *this;
				substituteStep.currentNode = currentNode->argumentChild;
				substituteStep.match.nodesPassed.push_back(substituteStep.currentNode);
				substituteStep.sourceElementIndex++;
				if (elementToCompare.type == PatternElement::Type::VariableLike) {
					size_t lineStart = patternReference->pattern.getLinePos(patternPos);
					size_t lineEnd = patternReference->pattern.getLinePos(patternPos + elementToCompare.text.size());
					substituteStep.match.discoveredVariables.push_back({elementToCompare.text, lineStart, lineEnd});
				} else {
					if (!patternReference->function || sourceArgumentIndex >= patternReference->function->arguments.size())
						return false;
					substituteStep.match.arguments.push_back(patternReference->function->arguments[sourceArgumentIndex]);
					substituteStep.sourceArgumentIndex++;
				}
				substituteStep.patternPos += elementToCompare.text.size();
				nextMatches.push_back(substituteStep);
				return true;
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

				delete subMatch.parent;
				subMatch.parent = new MatchProgress(*this);
				subMatch.parent->currentNode = currentNode->argumentChild;
				subMatch.parent->match.nodesPassed.push_back(subMatch.parent->currentNode);
				nextMatches.push_back(subMatch);
			};

			if (preferFunctionSubmatch) {
				if (!pushArgumentCapture())
					return nextMatches;
				pushSubmatch();
			} else {
				pushSubmatch();
				if (!pushArgumentCapture())
					return nextMatches;
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
			wordStep.match.discoveredWords.push_back({elementToCompare.text, lineStart, lineEnd});
			wordStep.patternPos += elementToCompare.text.size();
			nextMatches.push_back(wordStep);
		}
		// most priority: text match
		if (elementToCompare.type != PatternElement::Type::Variable &&
			currentNode->literalChildren.contains(elementToCompare.text)) {
			MatchProgress elemStep = *this;
			elemStep.currentNode = currentNode->literalChildren[elementToCompare.text];
			elemStep.match.nodesPassed.push_back(elemStep.currentNode);
			elemStep.sourceElementIndex++;
			elemStep.patternPos += elementToCompare.text.size();
			nextMatches.push_back(elemStep);
		}
	}
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
