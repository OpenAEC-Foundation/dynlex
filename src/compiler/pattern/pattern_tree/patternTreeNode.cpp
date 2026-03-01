#include "patternTreeNode.h"
#include "patternDefinition.h"
#include <algorithm>
#include <unordered_set>

// Link all parent nodes to a shared child for the given element.
// Reuses existing children where possible; creates one shared new child for parents that lack one.
static std::vector<PatternTreeNode *> addSharedChild(
	const std::vector<PatternTreeNode *> &parents, const DefinitionPatternElement &elem, PatternDefinition *definition
) {
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
	std::vector<PatternTreeNode *> currentNodes, const std::vector<DefinitionPatternElement> &elements,
	PatternDefinition *definition
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
	const std::vector<DefinitionPatternElement> &elements, size_t index, const std::vector<PatternTreeNode *> &mainNodes,
	const std::vector<PatternTreeNode *> &lessSpecificNodes, std::vector<PatternDefinition *> &result
) {
	if (index >= elements.size()) {
		// Collect matchingDefinitions from lessSpecific endpoints that are NOT on main endpoints
		std::unordered_set<PatternTreeNode *> mainSet(mainNodes.begin(), mainNodes.end());
		for (PatternTreeNode *node : lessSpecificNodes) {
			if (!node->matchingDefinitions.empty() && !mainSet.contains(node)) {
				for (auto *def : node->matchingDefinitions)
					result.push_back(def);
			}
		}
		return;
	}

	const DefinitionPatternElement &elem = elements[index];

	if (elem.type == PatternElement::Type::Choice) {
		// Branch each alternative recursively, then merge endpoints
		std::vector<PatternTreeNode *> mainEndpoints, lessEndpoints;
		for (const auto &alternative : elem.alternatives) {
			// Build a combined element list: this alternative + remaining elements after the choice
			std::vector<DefinitionPatternElement> subElements(alternative.begin(), alternative.end());
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
		// An argument node in a less-specific pattern can match a sub-function spanning
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

std::vector<PatternDefinition *> PatternTreeNode::findLessSpecificDefinitions(std::vector<DefinitionPatternElement> &elements) {
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

PatternDefinition *
PatternTreeNode::addPatternPart(std::vector<DefinitionPatternElement> &elements, PatternDefinition *definition, size_t index) {
	std::vector<DefinitionPatternElement> remaining(elements.begin() + index, elements.end());
	auto endpoints = addElementSequence({this}, remaining, definition);

	// Check if the new definition has any type constraints
	bool hasTypeConstraints = false;
	for (auto &elem : elements) {
		if (!elem.typeConstraintName.empty()) {
			hasTypeConstraints = true;
			break;
		}
	}

	for (auto *node : endpoints) {
		// Check for conflicts: a duplicate exists if an existing definition at this endpoint
		// has no type constraints AND the new definition has no type constraints.
		// Type-constrained overloads are allowed to coexist.
		if (!hasTypeConstraints) {
			for (auto *existingDef : node->matchingDefinitions) {
				if (existingDef == definition)
					continue;
				// Check if the existing definition also has no type constraints
				bool existingHasConstraints = false;
				for (auto &elem : existingDef->patternElements) {
					if (!elem.typeConstraintName.empty()) {
						existingHasConstraints = true;
						break;
					}
				}
				if (!existingHasConstraints)
					return existingDef;
			}
		}
		// Add this definition to the endpoint
		if (std::find(node->matchingDefinitions.begin(), node->matchingDefinitions.end(), definition) ==
			node->matchingDefinitions.end()) {
			node->matchingDefinitions.push_back(definition);
		}
	}
	return nullptr;
}

// Recursively remove a definition from the tree, walking the element path.
// Only removes the definition from matchingDefinitions/parameterNames at the endpoint.
// Does NOT detach empty nodes — keeping the trie structure intact ensures that
// addElementSequence during re-add finds the same nodes, preserving existing
// PatternMatch::matchedEndNode pointers.
static void removeDefinitionPath(
	PatternTreeNode *current, const std::vector<DefinitionPatternElement> &elements, size_t index, PatternDefinition *definition
) {
	if (index >= elements.size()) {
		// Endpoint: remove this definition from matchingDefinitions
		auto &defs = current->matchingDefinitions;
		defs.erase(std::remove(defs.begin(), defs.end(), definition), defs.end());
		current->parameterNames.erase(definition);
		return;
	}

	const DefinitionPatternElement &elem = elements[index];

	if (elem.type == PatternElement::Type::Choice) {
		for (const auto &alternative : elem.alternatives) {
			std::vector<DefinitionPatternElement> subElements(alternative.begin(), alternative.end());
			subElements.insert(subElements.end(), elements.begin() + index + 1, elements.end());
			removeDefinitionPath(current, subElements, 0, definition);
		}
		return;
	}

	// Find the child node for this element
	PatternTreeNode *child = nullptr;
	if (elem.type == PatternElement::Type::Variable) {
		child = current->argumentChild;
	} else if (elem.type == PatternElement::Type::Word) {
		child = current->wordChild;
	} else {
		auto it = current->literalChildren.find(elem.text);
		if (it != current->literalChildren.end())
			child = it->second;
	}

	if (!child)
		return;

	// Clean parameterNames on argument/word nodes
	if (elem.type == PatternElement::Type::Variable || elem.type == PatternElement::Type::Word)
		child->parameterNames.erase(definition);

	removeDefinitionPath(child, elements, index + 1, definition);
}

void PatternTreeNode::removePatternPart(std::vector<DefinitionPatternElement> &elements, PatternDefinition *definition) {
	removeDefinitionPath(this, elements, 0, definition);
}
