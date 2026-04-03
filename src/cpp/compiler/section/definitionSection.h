#pragma once
#include "section.h"

// Shared base class for EffectSection and FunctionSection
struct DefinitionSection : public Section {
	inline DefinitionSection(SectionType type, Section *parent = {}) : Section(type, parent) {}

	virtual bool processLine(ParseContext &context, CodeLine *line) override;
	virtual Section *createSection(ParseContext &context, CodeLine *line) override;
	// The replacement: or execute: section that should run when this pattern is called.
	// A definition becomes a flex automatically once it has a replacement section.
	Section *executionSection{};

	std::string toString() const override {
		if (!patternDefinitions.empty())
			return patternDefinitions[0]->toString();
		return Section::toString();
	}
};
