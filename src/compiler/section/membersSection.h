#pragma once
#include "section.h"

struct MembersSection : public Section {
	MembersSection(Section *parent) : Section(SectionType::Members, parent) {}

	virtual bool processLine(ParseContext &context, CodeLine *line) override;
	virtual Section *createSection(ParseContext &context, CodeLine *line) override;
};
