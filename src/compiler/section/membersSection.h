#pragma once
#include "classDefinition.h"
#include "listingSection.h"

struct MembersSection : public ListingSection {
	MembersSection(Section *parent) : ListingSection(SectionType::Members, parent) {}

	virtual bool processLine(ParseContext &context, CodeLine *line) override;
	virtual bool addItem(ParseContext &context, std::string_view item, CodeLine *line) override;
};

bool parseFieldDeclaration(ParseContext &context, std::string_view fieldText, CodeLine *line, struct ClassSection *section);