#pragma once
#include "classDefinition.h"
#include "listingSection.h"

struct MembersSection : public ListingSection {
	MembersSection(Section *parent) : ListingSection(SectionType::Members, parent) {}

	virtual bool processLine(ParseContext &context, CodeLine *line) override;
	virtual bool addItem(ParseContext &context, Range itemRange) override;
	virtual void addSeparator(ParseContext &context, Range separatorRange) override;
};

bool parseFieldDeclaration(ParseContext &context, Range fieldRange, struct ClassSection *section);
