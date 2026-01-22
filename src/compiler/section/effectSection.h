#pragma once
#include "section.h"
struct EffectSection : public Section {
	inline EffectSection(Section *parent = {}) : Section(SectionType::Effect, parent){};
	virtual bool processLine(ParseContext &context, CodeLine *line) override;
	virtual Section *createSection(ParseContext &context, CodeLine *line) override;
};