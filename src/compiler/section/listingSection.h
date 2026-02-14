#pragma once
#include "section.h"
#include <string_view>

// Base class for sections that parse lists (members, globals, etc.)
// Supports both newline-separated and comma-separated items:
//   members:
//       x, y, z
//       w
struct ListingSection : public Section {
	inline ListingSection(SectionType type, Section *parent) : Section(type, parent) {}

	virtual bool processLine(ParseContext &context, CodeLine *line) override;
	virtual Section *createSection(ParseContext &context, CodeLine *line) override;

	// Override this to handle each item in the list
	virtual void addItem(ParseContext &context, std::string_view item, CodeLine *line) = 0;
};
