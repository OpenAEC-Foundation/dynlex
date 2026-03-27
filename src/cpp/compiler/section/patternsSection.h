#pragma once
#include "section.h"
struct PatternsSection : public Section {
	PatternsSection(Section *parent) : Section(SectionType::Pattern, parent) {}

	virtual bool processLine(ParseContext &context, CodeLine *line) override;
	virtual Section *createSection(ParseContext &context, CodeLine *line) override;
};
