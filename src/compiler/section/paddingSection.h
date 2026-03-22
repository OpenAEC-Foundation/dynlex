#pragma once
#include "integerSettingSection.h"

struct PaddingSection : public IntegerSettingSection {
	PaddingSection(Section *parent) : IntegerSettingSection(SectionType::Padding, parent) {}

	bool applyValue(ParseContext &context, CodeLine *line, int value) override;
};
