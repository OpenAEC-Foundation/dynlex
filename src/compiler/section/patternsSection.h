#pragma once
#include "section.h"
struct PatternsSection : public Section {
	virtual bool processLine(ParseContext &context, CodeLine *line) override;
	virtual Section *createSection(ParseContext &context, CodeLine *line) override;
};