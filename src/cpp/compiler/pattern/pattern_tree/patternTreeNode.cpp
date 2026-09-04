#include "patternTreeNode.h"
#include "compilerUtils.h"
#include "patternDefinition.h"
#include <algorithm>
#include <unordered_set>

static PatternTreeNode *addChild(PatternTreeNode *parent, const PatternElement &element, PatternDefinition *definition) {
	PatternTreeNode *child = nullptr;
	if (element.type == PatternElement::Type::Variable) {
		child = parent->argumentChild;
	} else {
		auto existing = parent->literalChildren.find(element.text);
		if (existing != parent->literalChildren.end())
			child = existing->second;
	}

	if (!child) {
		child = new PatternTreeNode(element.type, element.text);
		if (element.type == PatternElement::Type::Variable)
			parent->argumentChild = child;
		else
			parent->literalChildren[element.text] = child;
	}
	std::string parameterName;
	if (element.type == PatternElement::Type::Variable)
		parameterName = element.text;
	child->definitionOccurrences[definition].push_back({element.startPos, std::move(parameterName)});
	return child;
}

static std::vector<PatternTreeNode *>
addElementPath(PatternTreeNode *current, const std::vector<PatternElement> &path, PatternDefinition *definition) {
	std::vector<PatternTreeNode *> nodes;
	nodes.reserve(path.size());
	for (const PatternElement &element : path) {
		current = addChild(current, element, definition);
		nodes.push_back(current);
	}
	return nodes;
}

static std::vector<std::vector<PatternElement>>
snapshotCanonicalPatternPaths(const std::vector<DefinitionPatternElement> &elements) {
	std::vector<std::vector<PatternElement>> result;
	for (const auto &path : canonicalPatternPaths(elements)) {
		std::vector<PatternElement> snapshot;
		snapshot.reserve(path.size());
		for (const DefinitionPatternElement &element : path)
			snapshot.emplace_back(element.type, element.text, element.startPos);
		result.push_back(std::move(snapshot));
	}
	return result;
}

static PatternTreeNode *findChild(PatternTreeNode *current, const PatternElement &element) {
	if (element.type == PatternElement::Type::Variable)
		return current->argumentChild;
	auto existing = current->literalChildren.find(element.text);
	return existing == current->literalChildren.end() ? nullptr : existing->second;
}

static bool
pathsEqual(const std::vector<std::vector<PatternElement>> &left, const std::vector<std::vector<PatternElement>> &right) {
	if (left.size() != right.size())
		return false;
	for (size_t pathIndex = 0; pathIndex < left.size(); pathIndex++) {
		if (left[pathIndex].size() != right[pathIndex].size())
			return false;
		for (size_t elementIndex = 0; elementIndex < left[pathIndex].size(); elementIndex++) {
			const PatternElement &leftElement = left[pathIndex][elementIndex];
			const PatternElement &rightElement = right[pathIndex][elementIndex];
			if (leftElement.type != rightElement.type || leftElement.text != rightElement.text ||
				leftElement.startPos != rightElement.startPos)
				return false;
		}
	}
	return true;
}

// Walk elements through two parallel paths in the tree: the main (exact) path and
// a "less specific" path that follows argument alternatives where the new definition
// has a more specific literal element. Any matchingDefinition found only on
// the less-specific endpoints is a less-specific definition.
static void walkForLessSpecific(
	const std::vector<DefinitionPatternElement> &path, size_t index, std::vector<PatternTreeNode *> mainNodes,
	std::vector<PatternTreeNode *> lessSpecificNodes, std::vector<PatternDefinition *> &result
) {
	if (index >= path.size()) {
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

	const DefinitionPatternElement &element = path[index];

	auto advanceThroughElement = [](const DefinitionPatternElement &elem, std::vector<PatternTreeNode *> &mainNodes,
									std::vector<PatternTreeNode *> &lessSpecificNodes) {
		std::vector<PatternTreeNode *> nextMain, nextLess;

		auto advanceNode = [&](PatternTreeNode *node, bool isMainPath) {
			// Check if this node is an argument node that can absorb multiple elements.
			// An argument node in a less-specific pattern can match a sub-expression spanning
			// multiple elements. Keep such nodes in nextLess so they continue absorbing.
			bool isAbsorbingArgNode = !isMainPath && node->type == PatternElement::Type::Variable;

			if (isAbsorbingArgNode) {
				// This argument node is on the less-specific path and can absorb this element.
				// Keep it in nextLess (absorbs more elements) AND try advancing through its children
				// (in case the argument ends here and the pattern continues).
				nextLess.push_back(node); // continue absorbing
				// Also try children for what comes after the argument
				auto it = node->literalChildren.find(elem.text);
				if (it != node->literalChildren.end())
					nextLess.push_back(it->second);
				if (node->argumentChild)
					nextLess.push_back(node->argumentChild);
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
			} else {
				// Literal (Other/VariableLike text): main follows literalChildren;
				// argumentChild is less specific
				if (isMainPath) {
					auto it = node->literalChildren.find(elem.text);
					if (it != node->literalChildren.end())
						nextMain.push_back(it->second);
					// Fork: an argument at this position is less specific
					if (node->argumentChild)
						nextLess.push_back(node->argumentChild);
				} else {
					// lessSpecific path continues through all possible children
					auto it = node->literalChildren.find(elem.text);
					if (it != node->literalChildren.end())
						nextLess.push_back(it->second);
					if (node->argumentChild)
						nextLess.push_back(node->argumentChild);
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

	advanceThroughElement(element, mainNodes, lessSpecificNodes);

	if (!mainNodes.empty() || !lessSpecificNodes.empty())
		walkForLessSpecific(path, index + 1, std::move(mainNodes), std::move(lessSpecificNodes), result);
}

std::vector<PatternDefinition *> PatternTreeNode::findLessSpecificDefinitions(std::vector<DefinitionPatternElement> &elements) {
	std::vector<PatternDefinition *> result;
	for (const auto &path : canonicalPatternPaths(elements))
		walkForLessSpecific(path, 0, {this}, {}, result);
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
	std::sort(result.begin(), result.end(), patternDefinitionComesBefore);
	return result;
}

void PatternTreeNode::addPatternDefinition(PatternDefinition *definition, SectionType treeType) {
	requireCompilerInvariant(definition != nullptr, "pattern tree insertion requires a definition");
	requireCompilerInvariant(
		definition->indexedTree == nullptr && definition->indexedTreeType == SectionType::Count &&
			definition->indexedPaths.empty() && definition->indexedNodePaths.empty() && definition->endNodes.empty(),
		"pattern tree insertion received an already-indexed definition"
	);
	definition->indexedPaths = snapshotCanonicalPatternPaths(definition->patternElements);
	definition->indexedTree = this;
	definition->indexedTreeType = treeType;
	std::unordered_set<PatternTreeNode *> seenEndpoints;
	for (const auto &path : definition->indexedPaths) {
		std::vector<PatternTreeNode *> nodePath = addElementPath(this, path, definition);
		PatternTreeNode *endpoint = nodePath.empty() ? this : nodePath.back();
		definition->indexedNodePaths.push_back(std::move(nodePath));
		if (seenEndpoints.insert(endpoint).second)
			definition->endNodes.push_back(endpoint);
		// Add this definition to the endpoint
		if (std::find(endpoint->matchingDefinitions.begin(), endpoint->matchingDefinitions.end(), definition) ==
			endpoint->matchingDefinitions.end()) {
			endpoint->matchingDefinitions.push_back(definition);
			endpoint->endpointRevision++;
		}
	}
	requirePatternDefinitionIndexed(definition);
}

// Recursively remove a definition from the tree, walking the element path.
// Only removes the definition from matchingDefinitions at the endpoint.
// Does NOT detach empty nodes — keeping the trie structure intact ensures that
// insertion during re-add finds the same nodes, preserving existing
// PatternMatch::matchedEndNode pointers.
// Step to the child node for one element, cleaning this definition's metadata
// on the way. Returns nullptr when the path does not exist.
static PatternTreeNode *stepAndCleanChild(PatternTreeNode *current, const PatternElement &elem, PatternDefinition *definition) {
	PatternTreeNode *child = findChild(current, elem);
	if (!child)
		return nullptr;
	child->definitionOccurrences.erase(definition);
	return child;
}

static void
removeDefinitionPath(PatternTreeNode *current, const std::vector<PatternElement> &path, PatternDefinition *definition) {
	for (const PatternElement &element : path) {
		current = stepAndCleanChild(current, element, definition);
		if (!current)
			crashCompilerBug("pattern tree removal lost a stored indexed path");
	}
	// Endpoint: remove this definition from matchingDefinitions
	auto &defs = current->matchingDefinitions;
	size_t definitionCount = defs.size();
	defs.erase(std::remove(defs.begin(), defs.end(), definition), defs.end());
	if (defs.size() != definitionCount)
		current->endpointRevision++;
	current->definitionOccurrences.erase(definition);
}

static void requireDefinitionAbsentFromPaths(
	PatternTreeNode *root, const std::vector<std::vector<PatternElement>> &paths, const PatternDefinition *definition
) {
	for (const auto &path : paths) {
		PatternTreeNode *current = root;
		for (const PatternElement &element : path) {
			current = findChild(current, element);
			requireCompilerInvariant(current != nullptr, "stored pattern path disappeared during removal");
			requireCompilerInvariant(
				!current->definitionOccurrences.contains(const_cast<PatternDefinition *>(definition)),
				"obsolete pattern path retains definition occurrence metadata"
			);
		}
		requireCompilerInvariant(
			std::find(current->matchingDefinitions.begin(), current->matchingDefinitions.end(), definition) ==
				current->matchingDefinitions.end(),
			"obsolete pattern endpoint retains an active definition"
		);
	}
}

void PatternTreeNode::removePatternDefinition(PatternDefinition *definition) {
	requireCompilerInvariant(definition != nullptr, "pattern tree removal requires a definition");
	requireCompilerInvariant(definition->indexedTree == this, "pattern tree removal used the wrong tree");
	requirePatternDefinitionIndexed(definition);
	std::vector<std::vector<PatternElement>> oldPaths = definition->indexedPaths;
	for (const auto &path : oldPaths)
		removeDefinitionPath(this, path, definition);
	requireDefinitionAbsentFromPaths(this, oldPaths, definition);
	definition->indexedTree = nullptr;
	definition->indexedTreeType = SectionType::Count;
	definition->indexedPaths.clear();
	definition->indexedNodePaths.clear();
	definition->endNodes.clear();
}

void PatternTreeNode::requirePatternDefinitionIndexed(const PatternDefinition *definition) const {
	requireCompilerInvariant(definition != nullptr, "pattern tree validation requires a definition");
	requireCompilerInvariant(definition->indexedTree == this, "indexed pattern definition records the wrong tree");
	requireCompilerInvariant(
		definition->indexedTreeType != SectionType::Count, "indexed pattern definition is missing its tree type"
	);
	requireCompilerInvariant(
		pathsEqual(definition->indexedPaths, snapshotCanonicalPatternPaths(definition->patternElements)),
		"indexed pattern paths do not match the definition's current canonical paths"
	);
	requireCompilerInvariant(
		definition->indexedNodePaths.size() == definition->indexedPaths.size(),
		"indexed pattern node paths do not match stored element paths"
	);

	std::vector<PatternTreeNode *> expectedEndpoints;
	std::unordered_set<PatternTreeNode *> seenEndpoints;
	std::unordered_map<PatternTreeNode *, std::vector<PatternDefinitionOccurrence>> expectedOccurrences;
	for (size_t pathIndex = 0; pathIndex < definition->indexedPaths.size(); pathIndex++) {
		const auto &path = definition->indexedPaths[pathIndex];
		const auto &storedNodePath = definition->indexedNodePaths[pathIndex];
		requireCompilerInvariant(storedNodePath.size() == path.size(), "indexed pattern node path has the wrong element count");
		PatternTreeNode *current = const_cast<PatternTreeNode *>(this);
		for (size_t elementIndex = 0; elementIndex < path.size(); elementIndex++) {
			const PatternElement &element = path[elementIndex];
			current = findChild(current, element);
			requireCompilerInvariant(current != nullptr, "indexed pattern snapshot has no trie path");
			requireCompilerInvariant(
				storedNodePath[elementIndex] == current, "indexed pattern node path does not match its trie path"
			);
			std::string parameterName;
			if (element.type == PatternElement::Type::Variable)
				parameterName = element.text;
			expectedOccurrences[current].push_back({element.startPos, std::move(parameterName)});
		}
		requireCompilerInvariant(
			std::find(current->matchingDefinitions.begin(), current->matchingDefinitions.end(), definition) !=
				current->matchingDefinitions.end(),
			"indexed pattern endpoint is missing its definition"
		);
		if (seenEndpoints.insert(current).second)
			expectedEndpoints.push_back(current);
	}
	for (const auto &[node, occurrences] : expectedOccurrences) {
		auto storedOccurrences = node->definitionOccurrences.find(const_cast<PatternDefinition *>(definition));
		requireCompilerInvariant(
			storedOccurrences != node->definitionOccurrences.end() && storedOccurrences->second == occurrences,
			"indexed pattern occurrence metadata does not match its definition"
		);
	}
	requireCompilerInvariant(expectedEndpoints == definition->endNodes, "indexed pattern endpoints do not match stored paths");
}
