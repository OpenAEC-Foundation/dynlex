#pragma once
#include "pattern_tree/patternElement.h"
#include "range.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct PatternDefinition;

struct ExplicitParameterCandidate {
	PatternDefinition *definition{};
	PatternElement::Type captureType = PatternElement::Type::Count;
	Range sourceRange;
};

class ExplicitParameterIndex {
  public:
	void addDefinition(PatternDefinition &definition);
	const std::vector<ExplicitParameterCandidate> *find(const std::string &name) const;
	bool contains(const PatternDefinition &definition, const std::string &name) const;

  private:
	std::unordered_map<std::string, std::vector<ExplicitParameterCandidate>> candidatesByName;
	std::unordered_set<PatternDefinition *> indexedDefinitions;
};
