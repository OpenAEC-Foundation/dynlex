#pragma once
#include "listingSection.h"

struct PrecedenceSection : public ListingSection {
	PrecedenceSection(SectionType type, Section *parent) : ListingSection(type, parent) {}

	virtual bool addItem(ParseContext &context, std::string_view item, CodeLine *line) override;
};
