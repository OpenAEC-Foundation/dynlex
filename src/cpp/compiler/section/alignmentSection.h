#pragma once
#include "integerSettingSection.h"

struct AlignmentSection : public IntegerSettingSection {
	AlignmentSection(Section *parent) : IntegerSettingSection(SectionType::Alignment, parent) {}

	bool applyValue(ParseContext &context, CodeLine *line, int value) override;
};
