#include "patternTreeNode.h"
#include "compilerUtils.h"
#include "patternDefinition.h"
#include <algorithm>
#include <climits>
#include <tuple>
#include <unordered_set>

namespace {
static std::tuple<int, int, int, std::string> definitionSortKey(const PatternDefinition *def) {
	if (!def)
		return {INT_MAX, INT_MAX, INT_MAX, ""};
	if (!def->range.line)
		return {INT_MAX - 1, def->range.start(), def->range.end(), def->toString()};
	return {def->range.line->mergedLineIndex, def->range.start(), def->range.end(), def->toString()};
}

static bool definitionComesBefore(const PatternDefinition *a, const PatternDefinition *b) {
	return definitionSortKey(a) < definitionSortKey(b);
}
} // namespace

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
			child->definitionStartPositions[definition] = elem.startPos;
			if (elem.type == PatternElement::Type::Variable || elem.type == PatternElement::Type::Word)
				child->parameterNames[definition] = elem.text;
			if (seen.insert(child).second)
				children.push_back(child);
		} else {
			// parent doesn't have a child — share one new node across all such parents
			if (!sharedNew)
				sharedNew = new PatternTreeNode(elem.type, elem.text);
			sharedNew->definitionStartPositions[definition] = elem.startPos;
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

// Tree walks canonicalize separator spaces per path: a separator only exists
// between two content elements, doubled separators collapse to one, and
// leading or trailing separators vanish. This makes every composition of
// choice alternatives (including empty ones) produce cleanly separated paths
// regardless of where the author placed the spaces.
struct SeparatorWalkState {
	bool pendingSeparator = false;
	size_t separatorStartPos = 0;
	bool hasContent = false;
};

static bool isSeparatorElement(const DefinitionPatternElement &elem) {
	return elem.type == PatternElement::Type::Other && !elem.text.empty() &&
		   elem.text.find_first_not_of(' ') == std::string::npos;
}

// Combine one choice alternative with the elements following the choice, so a
// branch continues through the shared tail with its own separator state.
static std::vector<DefinitionPatternElement> flattenAlternative(
	const std::vector<DefinitionPatternElement> &alternative, const std::vector<DefinitionPatternElement> &elements,
	size_t choiceIndex
) {
	std::vector<DefinitionPatternElement> flattened(alternative.begin(), alternative.end());
	flattened.insert(flattened.end(), elements.begin() + choiceIndex + 1, elements.end());
	return flattened;
}

// Walk a sequence of elements through the tree, branching at Choice elements
// and converging all branches back to shared nodes afterward.
static std::vector<PatternTreeNode *> addElementSequence(
	std::vector<PatternTreeNode *> currentNodes, const std::vector<DefinitionPatternElement> &elements,
	PatternDefinition *definition, SeparatorWalkState state
) {
	for (size_t i = 0; i < elements.size(); i++) {
		const DefinitionPatternElement &elem = elements[i];
		if (elem.type == PatternElement::Type::Choice) {
			std::vector<PatternTreeNode *> branchEndpoints;
			for (auto &alternative : elem.alternatives) {
				auto endpoints =
					addElementSequence(currentNodes, flattenAlternative(alternative, elements, i), definition, state);
				branchEndpoints.insert(branchEndpoints.end(), endpoints.begin(), endpoints.end());
			}
			// deduplicate — branches that converged to the same node
			std::unordered_set<PatternTreeNode *> seen;
			std::vector<PatternTreeNode *> deduped;
			for (auto *node : branchEndpoints) {
				if (seen.insert(node).second)
					deduped.push_back(node);
			}
			return deduped;
		}
		if (isSeparatorElement(elem)) {
			if (state.hasContent && !state.pendingSeparator) {
				state.pendingSeparator = true;
				state.separatorStartPos = elem.startPos;
			}
			continue;
		}
		if (state.pendingSeparator) {
			currentNodes = addSharedChild(
				currentNodes, DefinitionPatternElement(PatternElement::Type::Other, " ", state.separatorStartPos), definition
			);
			state.pendingSeparator = false;
		}
		currentNodes = addSharedChild(currentNodes, elem, definition);
		state.hasContent = true;
	}
	return currentNodes;
}

// Walk elements through two parallel paths in the tree: the main (exact) path and
// a "less specific" path that follows argument/word alternatives where the new definition
// has a more specific element (literal or word). Any matchingDefinition found only on
// the less-specific endpoints is a less-specific definition.
static void walkForLessSpecific(
	const std::vector<DefinitionPatternElement> &elements, size_t index, std::vector<PatternTreeNode *> mainNodes,
	std::vector<PatternTreeNode *> lessSpecificNodes, std::vector<PatternDefinition *> &result, SeparatorWalkState state
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

	const DefinitionPatternElement &choiceOrContent = elements[index];

	if (choiceOrContent.type == PatternElement::Type::Choice) {
		// Branch each alternative recursively, then merge endpoints
		for (const auto &alternative : choiceOrContent.alternatives) {
			std::vector<PatternDefinition *> subResult;
			walkForLessSpecific(
				flattenAlternative(alternative, elements, index), 0, mainNodes, lessSpecificNodes, subResult, state
			);
			result.insert(result.end(), subResult.begin(), subResult.end());
		}
		return;
	}

	if (isSeparatorElement(choiceOrContent)) {
		state.pendingSeparator = state.pendingSeparator || state.hasContent;
		walkForLessSpecific(elements, index + 1, std::move(mainNodes), std::move(lessSpecificNodes), result, state);
		return;
	}

	auto advanceThroughElement = [](const DefinitionPatternElement &elem, std::vector<PatternTreeNode *> &mainNodes,
									std::vector<PatternTreeNode *> &lessSpecificNodes) {
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

		mainNodes = std::move(nextMain);
		lessSpecificNodes = std::move(nextLess);
	};

	if (state.pendingSeparator) {
		advanceThroughElement(
			DefinitionPatternElement(PatternElement::Type::Other, " ", state.separatorStartPos), mainNodes, lessSpecificNodes
		);
		state.pendingSeparator = false;
		if (mainNodes.empty() && lessSpecificNodes.empty())
			return;
	}
	advanceThroughElement(choiceOrContent, mainNodes, lessSpecificNodes);
	state.hasContent = true;

	if (!mainNodes.empty() || !lessSpecificNodes.empty())
		walkForLessSpecific(elements, index + 1, std::move(mainNodes), std::move(lessSpecificNodes), result, state);
}

std::vector<PatternDefinition *> PatternTreeNode::findLessSpecificDefinitions(std::vector<DefinitionPatternElement> &elements) {
	std::vector<PatternDefinition *> result;
	walkForLessSpecific(elements, 0, {this}, {}, result, {});
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
	std::sort(result.begin(), result.end(), definitionComesBefore);
	return result;
}

void PatternTreeNode::addPatternPart(
	std::vector<DefinitionPatternElement> &elements, PatternDefinition *definition, size_t index
) {
	requireCompilerInvariant(definition != nullptr, "pattern tree insertion requires a definition");
	std::vector<DefinitionPatternElement> remaining(elements.begin() + index, elements.end());
	SeparatorWalkState state;
	state.hasContent = index > 0;
	auto endpoints = addElementSequence({this}, remaining, definition, state);
	definition->endNodes = endpoints;

	for (auto *node : endpoints) {
		// Add this definition to the endpoint
		if (std::find(node->matchingDefinitions.begin(), node->matchingDefinitions.end(), definition) ==
			node->matchingDefinitions.end()) {
			node->matchingDefinitions.push_back(definition);
		}
	}
}

// Recursively remove a definition from the tree, walking the element path.
// Only removes the definition from matchingDefinitions/parameterNames at the endpoint.
// Does NOT detach empty nodes — keeping the trie structure intact ensures that
// addElementSequence during re-add finds the same nodes, preserving existing
// PatternMatch::matchedEndNode pointers.
// Step to the child node for one element, cleaning this definition's metadata
// on the way. Returns nullptr when the path does not exist.
static PatternTreeNode *
stepAndCleanChild(PatternTreeNode *current, const DefinitionPatternElement &elem, PatternDefinition *definition) {
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
		return nullptr;
	child->definitionStartPositions.erase(definition);
	if (elem.type == PatternElement::Type::Variable || elem.type == PatternElement::Type::Word)
		child->parameterNames.erase(definition);
	return child;
}

static void removeDefinitionPath(
	PatternTreeNode *current, const std::vector<DefinitionPatternElement> &elements, PatternDefinition *definition,
	SeparatorWalkState state
) {
	for (size_t i = 0; i < elements.size(); i++) {
		const DefinitionPatternElement &elem = elements[i];
		if (elem.type == PatternElement::Type::Choice) {
			for (const auto &alternative : elem.alternatives)
				removeDefinitionPath(current, flattenAlternative(alternative, elements, i), definition, state);
			return;
		}
		if (isSeparatorElement(elem)) {
			state.pendingSeparator = state.pendingSeparator || state.hasContent;
			continue;
		}
		if (state.pendingSeparator) {
			current = stepAndCleanChild(
				current, DefinitionPatternElement(PatternElement::Type::Other, " ", elem.startPos), definition
			);
			if (!current)
				crashCompilerBug("pattern tree removal lost its separator path; elements changed since insertion");
			state.pendingSeparator = false;
		}
		current = stepAndCleanChild(current, elem, definition);
		if (!current)
			crashCompilerBug("pattern tree removal lost its element path; elements changed since insertion");
		state.hasContent = true;
	}
	// Endpoint: remove this definition from matchingDefinitions
	auto &defs = current->matchingDefinitions;
	defs.erase(std::remove(defs.begin(), defs.end(), definition), defs.end());
	current->parameterNames.erase(definition);
	current->definitionStartPositions.erase(definition);
}

void PatternTreeNode::removePatternPart(std::vector<DefinitionPatternElement> &elements, PatternDefinition *definition) {
	requireCompilerInvariant(definition != nullptr, "pattern tree removal requires a definition");
	removeDefinitionPath(this, elements, definition, {});
	definition->endNodes.clear();
}
