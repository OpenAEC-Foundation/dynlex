#pragma once
#include "definitionSection.h"

struct EffectSection : public DefinitionSection {
	inline EffectSection(Section *parent = {}) : DefinitionSection(SectionType::Effect, parent) {}

	virtual Section *createSection(ParseContext &context, CodeLine *line) override;
};