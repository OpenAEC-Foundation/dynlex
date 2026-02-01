#pragma once
#include "definitionSection.h"

struct SectionSection : public DefinitionSection {
	inline SectionSection(Section *parent = {}) : DefinitionSection(SectionType::Section, parent) {}

	virtual Section *createSection(ParseContext &context, CodeLine *line) override;
};
