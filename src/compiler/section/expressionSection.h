#pragma once
#include "section.h"
struct ExpressionSection : public Section {
	inline ExpressionSection(Section *parent = {}) : Section(SectionType::Expression, parent){};
	virtual bool processLine(ParseContext &context, CodeLine *line) override;
	virtual Section *createSection(ParseContext &context, CodeLine *line) override;
};