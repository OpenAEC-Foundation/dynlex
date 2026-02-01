#include "sectionSection.h"
#include "parseContext.h"

Section *SectionSection::createSection(ParseContext &context, CodeLine *line) {
	// SectionSection uses "execute" for its content
	if (line->patternText == "execute") {
		return new Section(SectionType::Custom, this);
	}

	// Fall back to base class (handles "replacement" for macros, or gives error)
	return DefinitionSection::createSection(context, line);
}
