#pragma once
#include "listingSection.h"

struct PrecedenceSection : public ListingSection {
	PrecedenceSection(SectionType type, Section *parent) : ListingSection(type, parent) {}

	virtual bool addItem(ParseContext &context, Range itemRange) override;
	virtual void addSeparator(ParseContext &context, Range separatorRange) override;
};
