#pragma once
#include "listingSection.h"

struct GlobalsSection : public ListingSection {
	GlobalsSection(Section *parent) : ListingSection(SectionType::Globals, parent) {}

	virtual bool addItem(ParseContext &context, std::string_view item, CodeLine *line) override;
};
