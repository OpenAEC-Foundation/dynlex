#pragma once
#include "section.h"

// Shared base class for EffectSection and ExpressionSection
struct DefinitionSection : public Section {
	inline DefinitionSection(SectionType type, Section *parent = {}) : Section(type, parent) {}

	virtual bool processLine(ParseContext &context, CodeLine *line) override;
	virtual Section *createSection(ParseContext &context, CodeLine *line) override;
};
