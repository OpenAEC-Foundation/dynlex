#pragma once
#include "classDefinition.h"
#include "listingSection.h"

struct MembersSection : public ListingSection {
	MembersSection(Section *parent) : ListingSection(SectionType::Members, parent) {}

	virtual bool processLine(ParseContext &context, CodeLine *line) override;
	virtual Section *createSection(ParseContext &context, CodeLine *line) override;
	virtual bool addItem(ParseContext &context, Range itemRange) override;
	virtual void addSeparator(ParseContext &context, Range separatorRange) override;
	virtual bool finalize(ParseContext &context) override;

	bool setNextFieldAlignment(ParseContext &context, CodeLine *line, unsigned alignment);
	unsigned takeNextFieldAlignment();

  private:
	unsigned nextFieldAlignment = 0;
	Range nextFieldAlignmentRange;
};

bool parseFieldDeclaration(ParseContext &context, Range fieldRange, struct ClassSection *section);
