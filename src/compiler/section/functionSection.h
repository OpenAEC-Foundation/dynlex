#pragma once
#include "definitionSection.h"

struct FunctionSection : public DefinitionSection {
	inline FunctionSection(Section *parent = {}) : DefinitionSection(SectionType::Function, parent) {}

	virtual Section *createSection(ParseContext &context, CodeLine *line) override;
};