#include "patternTreeNode.h"
#include <unordered_set>

// Link all parent nodes to a shared child for the given element.
// Reuses existing children where possible; creates one shared new child for parents that lack one.
static std::vector<PatternTreeNode *>
addSharedChild(const std::vector<PatternTreeNode *> &parents, const PatternElement &elem, PatternDefinition *definition) {
	PatternTreeNode *sharedNew = nullptr;
	std::vector<PatternTreeNode *> children;
	std::unordered_set<PatternTreeNode *> seen;

	for (auto *parent : parents) {
		PatternTreeNode *child = nullptr;

		if (elem.type == PatternElement::Type::Variable) {
			child = parent->argumentChild;
		} else {
			auto it = parent->literalChildren.find(elem.text);
			if (it != parent->literalChildren.end())
				child = it->second;
		}

		if (child) {
			// parent already has a child for this element — reuse it
			if (elem.type == PatternElement::Type::Variable)
				child->parameterNames[definition] = elem.text;
			if (seen.insert(child).second)
				children.push_back(child);
		} else {
			// parent doesn't have a child — share one new node across all such parents
			if (!sharedNew)
				sharedNew = new PatternTreeNode(elem.type, elem.text);
			if (elem.type == PatternElement::Type::Variable) {
				parent->argumentChild = sharedNew;
				sharedNew->parameterNames[definition] = elem.text;
			} else {
				parent->literalChildren[elem.text] = sharedNew;
			}
			if (seen.insert(sharedNew).second)
				children.push_back(sharedNew);
		}
	}

	return children;
}

// Walk a sequence of elements through the tree, branching at Choice elements
// and converging all branches back to shared nodes afterward.
static std::vector<PatternTreeNode *> addElementSequence(
	std::vector<PatternTreeNode *> currentNodes, const std::vector<PatternElement> &elements, PatternDefinition *definition
) {
	for (auto &elem : elements) {
		if (elem.type == PatternElement::Type::Choice) {
			std::vector<PatternTreeNode *> branchEndpoints;
			for (auto &alternative : elem.alternatives) {
				auto endpoints = addElementSequence(currentNodes, alternative, definition);
				branchEndpoints.insert(branchEndpoints.end(), endpoints.begin(), endpoints.end());
			}
			// deduplicate — branches that converged to the same node
			std::unordered_set<PatternTreeNode *> seen;
			currentNodes.clear();
			for (auto *node : branchEndpoints) {
				if (seen.insert(node).second)
					currentNodes.push_back(node);
			}
		} else {
			currentNodes = addSharedChild(currentNodes, elem, definition);
		}
	}
	return currentNodes;
}

void PatternTreeNode::addPatternPart(std::vector<PatternElement> &elements, PatternDefinition *definition, size_t index) {
	std::vector<PatternElement> remaining(elements.begin() + index, elements.end());
	auto endpoints = addElementSequence({this}, remaining, definition);
	for (auto *node : endpoints) {
		node->matchingDefinition = definition;
	}
}
