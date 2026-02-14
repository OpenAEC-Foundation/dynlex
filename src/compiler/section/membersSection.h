#pragma once
#include "listingSection.h"

struct MembersSection : public ListingSection {
	MembersSection(Section *parent) : ListingSection(SectionType::Members, parent) {}

	virtual bool processLine(ParseContext &context, CodeLine *line) override;
	virtual void addItem(ParseContext &context, std::string_view item, CodeLine *line) override;
};
