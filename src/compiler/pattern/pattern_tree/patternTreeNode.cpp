#include "patternTreeNode.h"

void PatternTreeNode::addPatternPart(std::vector<PatternElement> &elements, Section *matchingSection, size_t index) {
	PatternTreeNode *currentNode = this;
	for (; index < elements.size(); index++) {
		PatternElement currentElement = elements[index];
		if (currentElement.type == PatternElement::Type::Variable) {
			if (!currentNode->argumentChild) {
				currentNode->argumentChild = new PatternTreeNode(currentElement.type, currentElement.text);
			}
			currentNode = currentNode->argumentChild;

		} else {
			if (!currentNode->literalChildren.count(currentElement.text)) {
				currentNode->literalChildren[currentElement.text] =
					new PatternTreeNode(currentElement.type, currentElement.text);
			}
			currentNode = currentNode->literalChildren[currentElement.text];
		}
	}
	currentNode->matchingSection = matchingSection;
}