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
	// use implicit copy assignment to shallow copy all members,
	// then deep copy parent. this avoids maintaining a member list.
	*this = other;
	if (other.parent)
		parent = new MatchProgress(*other.parent);
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

	if (currentNode->matchingDefinition) {
		// end node found
		PatternDefinition *def = currentNode->matchingDefinition;

		// Precedence check: reject this completion if constraints are violated
		bool precedenceOK = true;
		if (def->precedence > 0) {
			if (def->precedence > maxPrecedence || def->precedence >= minRightPrecedence)
				precedenceOK = false;
		}

		if (precedenceOK) {
			if (!parent && sourceElementIndex == patternReference->patternElements.size()) {
				addMatchData(match);
			}

			if (canBeSubmatch()) {
				// this might be a submatch of a higher level match.
				if (parent) {
					// there already is a parent match which submatched, possibly for this match
					//  f.e: '$ + $' in 'set $ to $ + $'
					stepUp(*parent);
					// Case B: if this submatch fills a right-side argument (parent matched past first arg slot),
					// propagate the completed expression's precedence as a constraint.
					// Skip for default-level patterns — they capture full expressions and shouldn't
					// constrain parent operators (minRightPrecedence is for same-level associativity).
					if (parent->match.nodesPassed.size() > 1) {
						int rightPrec = (def->precedence > 0) ? def->precedence : INT_MAX;
						if (context->defaultPrecedenceLevel == 0 || rightPrec != context->defaultPrecedenceLevel)
							nextMatches.back().minRightPrecedence = std::min(nextMatches.back().minRightPrecedence, rightPrec);
					}
				}
				if (canStartSubmatch() && rootNode->argumentChild) {
					// this might be the first submatch of a higher level match between parent and this match,
					// making the current parent the grand parent.
					// f.e: 'the result' in 'the result = 10'
					// or: '$ + $' in 'set $ to $ + $ dollars'
					// set $ to $: grandparent
					// $ dollars: parent (just discovered)
					// $ + $: current match (we just finished matching this)
					MatchProgress clone = *this;
					// the old parent progress becomes 'grandparent'
					if (parent)
						clone.parent = new MatchProgress(*parent);
					clone.rootNode = rootNode;
					// advance past the argument slot — the completed sub-expression occupies it
					clone.currentNode = rootNode->argumentChild;
					clone.match = {};
					clone.match.nodesPassed.push_back(clone.currentNode);

					// Case C: completed match becomes LEFT argument of new operator
					clone.maxPrecedence = (def->precedence > 0) ? def->precedence : INT_MAX;

					clone.type = SectionType::Expression;
					stepUp(clone);
				}
			}
		}
	}
	if (sourceElementIndex < patternReference->patternElements.size()) {
		PatternElement elementToCompare = patternReference->patternElements[sourceElementIndex];

		// less priority: arguments
		if (currentNode->argumentChild) {

			if (canStartSubmatch()) {
				// substitute the following part of the pattern
				// don't increase sourceElementIndex for the submatch, we need to compare this element in the submatch
				MatchProgress subMatch = *this;
				subMatch.currentNode = context->patternTrees[(int)SectionType::Expression];
				subMatch.rootNode = subMatch.currentNode;
				subMatch.type = SectionType::Expression;
				subMatch.patternStartPos = patternPos;
				subMatch.match = {};

				// Case D: new submatch — reset precedence constraints (fresh expression parse)
				subMatch.maxPrecedence = INT_MAX;
				subMatch.minRightPrecedence = INT_MAX;

				// deleting null doesn't matter
				delete subMatch.parent;
				subMatch.parent = new MatchProgress(*this);
				subMatch.parent->currentNode = currentNode->argumentChild;
				subMatch.parent->match.nodesPassed.push_back(subMatch.parent->currentNode);
				nextMatches.push_back(subMatch);
			}

			// use an element as argument
			if (elementToCompare.type != PatternElement::Type::Other) {
				// variable or potential variable
				MatchProgress substituteStep = *this;
				// we continue on the branch that takes an argument now

				substituteStep.currentNode = currentNode->argumentChild;
				substituteStep.match.nodesPassed.push_back(substituteStep.currentNode);
				substituteStep.sourceElementIndex++;
				if (elementToCompare.type == PatternElement::Type::VariableLike) {
					size_t lineStart = patternReference->pattern.getLinePos(patternPos);
					size_t lineEnd = patternReference->pattern.getLinePos(patternPos + elementToCompare.text.size());
					substituteStep.match.discoveredVariables.push_back({elementToCompare.text, lineStart, lineEnd});
				} else {
					// argument
					substituteStep.match.arguments.push_back(patternReference->expression->arguments[sourceArgumentIndex]);
					substituteStep.sourceArgumentIndex++;
				}
				substituteStep.patternPos += elementToCompare.text.size();
				nextMatches.push_back(substituteStep);
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
	return type != SectionType::Expression || currentNode != rootNode;
}

bool MatchProgress::canBeSubmatch() const { return type == SectionType::Expression; }

void MatchProgress::addMatchData(PatternMatch &match) {
	match.matchedEndNode = currentNode;
	match.lineStartPos = patternReference->pattern.getLinePos(patternStartPos);
	match.lineEndPos = patternReference->pattern.getLinePos(patternPos);
}
