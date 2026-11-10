#pragma once
#include "section.h"

struct ReplacementSection : public Section {
	ReplacementSection(Section *parent) : Section(SectionType::Replacement, parent) {}
	bool isValueExpressionContext() const override { return true; }

	virtual bool processLine(ParseContext &context, CodeLine *line) override;
};
