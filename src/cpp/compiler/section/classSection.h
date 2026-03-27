#pragma once
#include "classDefinition.h"
#include "definitionSection.h"

struct ClassSection : public DefinitionSection {
	ClassSection(Section *parent) : DefinitionSection(SectionType::Class, parent) { classDefinition = new ClassDefinition(); }

	ClassDefinition *classDefinition;

	virtual bool processLine(ParseContext &context, CodeLine *line) override;
	virtual Section *createSection(ParseContext &context, CodeLine *line) override;
};
