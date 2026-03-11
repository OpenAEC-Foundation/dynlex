#pragma once
#include "listingSection.h"

struct GlobalsSection : public ListingSection {
	GlobalsSection(Section *parent) : ListingSection(SectionType::Globals, parent) {}

	virtual bool addItem(ParseContext &context, Range itemRange) override;
	virtual void addSeparator(ParseContext &context, Range separatorRange) override;
};
