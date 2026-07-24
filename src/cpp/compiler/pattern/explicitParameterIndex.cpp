#include "explicitParameterIndex.h"
#include "patternDefinition.h"
#include <algorithm>

void ExplicitParameterIndex::addDefinition(PatternDefinition &definition) {
	if (!indexedDefinitions.insert(&definition).second)
		return;
	forEachLeafElement(definition.patternElements, [&](const DefinitionPatternElement &element) {
		if (element.type != PatternElement::Type::Variable && element.type != PatternElement::Type::Word)
			return;
		int sourceStart = definition.range.start() + static_cast<int>(element.startPos);
		Range sourceRange(definition.range.line, sourceStart, sourceStart + static_cast<int>(element.text.length()));
		candidatesByName[element.text].push_back({&definition, element.type, sourceRange});
	});
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
