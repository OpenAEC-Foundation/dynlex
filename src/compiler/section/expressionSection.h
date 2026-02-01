#pragma once
#include "definitionSection.h"

struct ExpressionSection : public DefinitionSection {
	inline ExpressionSection(Section *parent = {}) : DefinitionSection(SectionType::Expression, parent) {}

	virtual Section *createSection(ParseContext &context, CodeLine *line) override;
};