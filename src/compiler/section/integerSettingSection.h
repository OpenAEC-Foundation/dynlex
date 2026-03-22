#pragma once
#include "section.h"

struct IntegerSettingSection : public Section {
	IntegerSettingSection(SectionType type, Section *parent) : Section(type, parent) {}

	bool processLine(ParseContext &context, CodeLine *line) override;
	Section *createSection(ParseContext &context, CodeLine *line) override;

	virtual bool applyValue(ParseContext &context, CodeLine *line, int value) = 0;
};
