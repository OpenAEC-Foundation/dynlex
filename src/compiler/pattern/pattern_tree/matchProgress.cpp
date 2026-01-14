#include "matchProgress.h"
#include "parseContext.h"
MatchProgress::MatchProgress(ParseContext *context, PatternReference *patternReference)
	: context(context), patternReference(patternReference) {

	patternReference->patternElements = getPatternElements(patternReference->pattern.text);
	rootNode = context->patternTrees[(int)patternReference->patternType];
	currentNode = rootNode;
}

MatchProgress::MatchProgress(const MatchProgress &other)
	: parent(other.parent ? new MatchProgress(*other.parent) : nullptr), context(other.context),type(other.type), rootNode(other.rootNode), currentNode(other.currentNode), result(other.result), patternReference(other.patternReference), nodesPassed(other.nodesPassed), sourceElementIndex(other.sourceElementIndex), sourceCharIndex(other.sourceCharIndex) {}

std::vector<MatchProgress> MatchProgress::step() {
	std::vector<MatchProgress> nextMatches = std::vector<MatchProgress>();
	PatternElement elementToCompare = patternReference->patternElements[sourceElementIndex];

	// less priority: arguments
	if (currentNode->argumentChild) {
		// submatch and use the result as argument for the parent progress
		auto stepUp = [&nextMatches, this](MatchProgress &parentProgress) {
			if (parentProgress.currentNode->argumentChild) {
				MatchProgress stepUp = parentProgress;
				stepUp.currentNode = context->patternTrees[(int)SectionType::Expression];
				stepUp.parent = new MatchProgress(parentProgress);
				stepUp.nodesPassed = {};
				stepUp.rootNode = stepUp.currentNode;
				stepUp.type = SectionType::Expression;
				nextMatches.push_back(stepUp);
			}
		};

		if (currentNode->matchingSection) {
			// end node found

			if (sourceElementIndex == patternReference->patternElements.size() && !parent) {
				// found a full match
				PatternMatch *fullMatch =
					new PatternMatch{.matchedEndNode = currentNode, .range = patternReference->pattern.text};
			}
			if (canBeSubstitute()) {
				// this might be a submatch of a higher level match.
				if (parent) {
					// there already is a parent match which submatched, possibly for this match
					//  f.e: '$ + $' in 'set $ to $ + $'
					stepUp(*parent);
				}
				if (canSubstitute()) {
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
					clone.currentNode = rootNode;

					clone.type = SectionType::Expression;
					stepUp(clone);
				}
			}
		}

		// use an element as argument
		if (elementToCompare.type != PatternElement::Type::Other) {
			// variable or potential variable
			MatchProgress substituteStep = *this;
			// we continue on the branch that takes an argument now

			substituteStep.currentNode = currentNode->argumentChild;
			substituteStep.nodesPassed.push_back(substituteStep.currentNode);
			substituteStep.sourceElementIndex++;
			nextMatches.push_back(substituteStep);
		}
	}
	// most priority: text match
	if (elementToCompare.type != PatternElement::Type::Variable && currentNode->literalChildren.count(elementToCompare.text)) {
		MatchProgress elemStep = *this;
		elemStep.currentNode = currentNode->literalChildren[elementToCompare.text];
		elemStep.nodesPassed.push_back(elemStep.currentNode);
		elemStep.sourceElementIndex++;
		nextMatches.push_back(elemStep);
	}
	return nextMatches;
}

bool MatchProgress::canSubstitute() const {
	// prevent infinite recursion
	return type != SectionType::Expression || currentNode != rootNode;
}

bool MatchProgress::canBeSubstitute() const { return type == SectionType::Expression; }
