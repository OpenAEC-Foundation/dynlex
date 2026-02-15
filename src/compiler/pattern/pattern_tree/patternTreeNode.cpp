#include "patternTreeNode.h"
#include <algorithm>
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
		} else if (elem.type == PatternElement::Type::Word) {
			child = parent->wordChild;
		} else {
			auto it = parent->literalChildren.find(elem.text);
			if (it != parent->literalChildren.end())
				child = it->second;
		}

		if (child) {
			// parent already has a child for this element — reuse it
			if (elem.type == PatternElement::Type::Variable || elem.type == PatternElement::Type::Word)
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
			} else if (elem.type == PatternElement::Type::Word) {
				parent->wordChild = sharedNew;
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

// Walk elements through two parallel paths in the tree: the main (exact) path and
// a "less specific" path that follows argument/word alternatives where the new definition
// has a more specific element (literal or word). Any matchingDefinition found only on
// the less-specific endpoints is a less-specific definition.
static void walkForLessSpecific(
	const std::vector<PatternElement> &elements, size_t index, const std::vector<PatternTreeNode *> &mainNodes,
	const std::vector<PatternTreeNode *> &lessSpecificNodes, std::vector<PatternDefinition *> &result
) {
	if (index >= elements.size()) {
		// Collect matchingDefinitions from lessSpecific endpoints that are NOT on main endpoints
		std::unordered_set<PatternTreeNode *> mainSet(mainNodes.begin(), mainNodes.end());
		for (PatternTreeNode *node : lessSpecificNodes) {
			if (node->matchingDefinition && !mainSet.contains(node))
				result.push_back(node->matchingDefinition);
		}
		return;
	}

	const PatternElement &elem = elements[index];

	if (elem.type == PatternElement::Type::Choice) {
		// Branch each alternative recursively, then merge endpoints
		std::vector<PatternTreeNode *> mainEndpoints, lessEndpoints;
		for (const auto &alternative : elem.alternatives) {
			// Build a combined element list: this alternative + remaining elements after the choice
			std::vector<PatternElement> subElements(alternative.begin(), alternative.end());
			subElements.insert(subElements.end(), elements.begin() + index + 1, elements.end());
			std::vector<PatternDefinition *> subResult;
			walkForLessSpecific(subElements, 0, mainNodes, lessSpecificNodes, subResult);
			result.insert(result.end(), subResult.begin(), subResult.end());
		}
		return;
	}

	std::vector<PatternTreeNode *> nextMain, nextLess;

	auto advanceNode = [&](PatternTreeNode *node, bool isMainPath) {
		// Check if this node is an argument/word node that can absorb multiple elements.
		// An argument node in a less-specific pattern can match a sub-expression spanning
		// multiple elements. Keep such nodes in nextLess so they continue absorbing.
		bool isAbsorbingArgNode =
			!isMainPath && (node->type == PatternElement::Type::Variable || node->type == PatternElement::Type::Word);

		if (isAbsorbingArgNode) {
			// This argument/word node is on the less-specific path and can absorb this element.
			// Keep it in nextLess (absorbs more elements) AND try advancing through its children
			// (in case the argument ends here and the pattern continues).
			nextLess.push_back(node); // continue absorbing
			// Also try children for what comes after the argument
			auto it = node->literalChildren.find(elem.text);
			if (it != node->literalChildren.end())
				nextLess.push_back(it->second);
			if (node->argumentChild)
				nextLess.push_back(node->argumentChild);
			if (node->wordChild)
				nextLess.push_back(node->wordChild);
			return;
		}

		if (elem.type == PatternElement::Type::Variable) {
			// Variable (argument slot): both paths follow argumentChild — same specificity
			if (node->argumentChild) {
				if (isMainPath)
					nextMain.push_back(node->argumentChild);
				else
					nextLess.push_back(node->argumentChild);
			}
		} else if (elem.type == PatternElement::Type::Word) {
			// Word: main follows wordChild; argumentChild is less specific
			if (isMainPath) {
				if (node->wordChild)
					nextMain.push_back(node->wordChild);
				// Fork: argumentChild is less specific than word
				if (node->argumentChild)
					nextLess.push_back(node->argumentChild);
			} else {
				// lessSpecific path continues through both word and argument
				if (node->wordChild)
					nextLess.push_back(node->wordChild);
				if (node->argumentChild)
					nextLess.push_back(node->argumentChild);
			}
		} else {
			// Literal (Other/VariableLike text): main follows literalChildren;
			// argumentChild and wordChild are less specific
			if (isMainPath) {
				auto it = node->literalChildren.find(elem.text);
				if (it != node->literalChildren.end())
					nextMain.push_back(it->second);
				// Fork: argument/word at this position is less specific
				if (node->argumentChild)
					nextLess.push_back(node->argumentChild);
				if (node->wordChild)
					nextLess.push_back(node->wordChild);
			} else {
				// lessSpecific path continues through all possible children
				auto it = node->literalChildren.find(elem.text);
				if (it != node->literalChildren.end())
					nextLess.push_back(it->second);
				if (node->argumentChild)
					nextLess.push_back(node->argumentChild);
				if (node->wordChild)
					nextLess.push_back(node->wordChild);
			}
		}
	};

	for (PatternTreeNode *node : mainNodes)
		advanceNode(node, true);
	for (PatternTreeNode *node : lessSpecificNodes)
		advanceNode(node, false);

	// Deduplicate
	auto dedup = [](std::vector<PatternTreeNode *> &v) {
		std::unordered_set<PatternTreeNode *> seen;
		v.erase(
			std::remove_if(
				v.begin(), v.end(),
				[&](PatternTreeNode *n) {
			return !seen.insert(n).second;
		}
			),
			v.end()
		);
	};
	dedup(nextMain);
	dedup(nextLess);
	// Remove from nextLess any node that's already in nextMain (they're on the exact path)
	{
		std::unordered_set<PatternTreeNode *> mainSet(nextMain.begin(), nextMain.end());
		nextLess.erase(
			std::remove_if(
				nextLess.begin(), nextLess.end(),
				[&](PatternTreeNode *n) {
			return mainSet.contains(n);
		}
			),
			nextLess.end()
		);
	}

	if (!nextMain.empty() || !nextLess.empty())
		walkForLessSpecific(elements, index + 1, nextMain, nextLess, result);
}

std::vector<PatternDefinition *> PatternTreeNode::findLessSpecificDefinitions(std::vector<PatternElement> &elements) {
	std::vector<PatternDefinition *> result;
	walkForLessSpecific(elements, 0, {this}, {}, result);
	// Deduplicate
	std::unordered_set<PatternDefinition *> seen;
	result.erase(
		std::remove_if(
			result.begin(), result.end(),
			[&](PatternDefinition *d) {
		return !seen.insert(d).second;
	}
		),
		result.end()
	);
	return result;
}

void PatternTreeNode::addPatternPart(std::vector<PatternElement> &elements, PatternDefinition *definition, size_t index) {
	std::vector<PatternElement> remaining(elements.begin() + index, elements.end());
	auto endpoints = addElementSequence({this}, remaining, definition);
	for (auto *node : endpoints) {
		node->matchingDefinition = definition;
	}
}

// Walk the tree following existing nodes (no creation) and collect endpoint nodes.
// Handles Choice elements by branching. Returns empty if path doesn't exist.
static std::vector<PatternTreeNode *>
followElementSequence(std::vector<PatternTreeNode *> currentNodes, const std::vector<PatternElement> &elements, PatternDefinition *definition) {
	for (const auto &elem : elements) {
		if (elem.type == PatternElement::Type::Choice) {
			std::vector<PatternTreeNode *> branchEndpoints;
			for (const auto &alternative : elem.alternatives) {
				auto endpoints = followElementSequence(currentNodes, alternative, definition);
				branchEndpoints.insert(branchEndpoints.end(), endpoints.begin(), endpoints.end());
			}
			std::unordered_set<PatternTreeNode *> seen;
			currentNodes.clear();
			for (auto *node : branchEndpoints) {
				if (seen.insert(node).second)
					currentNodes.push_back(node);
			}
		} else {
			std::vector<PatternTreeNode *> nextNodes;
			std::unordered_set<PatternTreeNode *> seen;
			for (auto *parent : currentNodes) {
				PatternTreeNode *child = nullptr;
				if (elem.type == PatternElement::Type::Variable) {
					child = parent->argumentChild;
				} else if (elem.type == PatternElement::Type::Word) {
					child = parent->wordChild;
				} else {
					auto it = parent->literalChildren.find(elem.text);
					if (it != parent->literalChildren.end())
						child = it->second;
				}
				if (child && seen.insert(child).second)
					nextNodes.push_back(child);
			}
			currentNodes = nextNodes;
		}
		if (currentNodes.empty())
			break;
	}
	return currentNodes;
}

void PatternTreeNode::removePatternPart(std::vector<PatternElement> &elements, PatternDefinition *definition) {
	std::vector<PatternElement> remaining(elements.begin(), elements.end());
	auto endpoints = followElementSequence({this}, remaining, definition);
	for (auto *node : endpoints) {
		if (node->matchingDefinition == definition)
			node->matchingDefinition = nullptr;
		node->parameterNames.erase(definition);
	}
	// Also clean parameterNames from intermediate argument/word nodes
	auto intermediates = followElementSequence({this}, remaining, definition);
	// Walk element by element to find argument/word nodes and clean their parameterNames
	std::vector<PatternTreeNode *> current = {this};
	for (const auto &elem : remaining) {
		if (elem.type == PatternElement::Type::Choice)
			continue; // simplified — Choice cleanup handled by endpoint cleanup
		std::vector<PatternTreeNode *> next;
		for (auto *parent : current) {
			PatternTreeNode *child = nullptr;
			if (elem.type == PatternElement::Type::Variable)
				child = parent->argumentChild;
			else if (elem.type == PatternElement::Type::Word)
				child = parent->wordChild;
			else {
				auto it = parent->literalChildren.find(elem.text);
				if (it != parent->literalChildren.end())
					child = it->second;
			}
			if (child) {
				child->parameterNames.erase(definition);
				next.push_back(child);
			}
		}
		current = next;
		if (current.empty())
			break;
	}
}
