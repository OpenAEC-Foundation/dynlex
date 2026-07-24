#include "explicitParameterIndex.h"
#include "patternDefinition.h"
#include <algorithm>

void ExplicitParameterIndex::addDefinition(PatternDefinition &definition) {
	if (!indexedDefinitions.insert(&definition).second)
		return;
	auto paths = canonicalPatternPaths(definition.patternElements);
	for (size_t pathIndex = 0; pathIndex < paths.size(); pathIndex++) {
		const auto &path = paths[pathIndex];
		for (size_t elementIndex = 0; elementIndex < path.size(); elementIndex++) {
			const DefinitionPatternElement &element = path[elementIndex];
			if (element.type != PatternElement::Type::Variable && element.type != PatternElement::Type::Word)
				continue;
			int sourceStart = definition.range.start() + static_cast<int>(element.startPos);
			Range sourceRange(definition.range.line, sourceStart, sourceStart + static_cast<int>(element.text.length()));
			candidatesByName[element.text].push_back(
				{&definition, pathIndex, elementIndex, element.type, element.startPos, sourceRange}
			);
		}
	}
}

const std::vector<ExplicitParameterCandidate> *ExplicitParameterIndex::find(const std::string &name) const {
	auto it = candidatesByName.find(name);
	return it == candidatesByName.end() ? nullptr : &it->second;
}

bool ExplicitParameterIndex::contains(const PatternDefinition &definition, const std::string &name) const {
	const auto *candidates = find(name);
	return candidates && std::any_of(candidates->begin(), candidates->end(), [&](const ExplicitParameterCandidate &candidate) {
		return candidate.definition == &definition;
	});
}
