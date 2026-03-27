#include "sectionSection.h"
#include "parseContext.h"

Section *SectionSection::createSection(ParseContext &context, CodeLine *line) {
	// Fall back to base class (handles "replacement" for macros, or gives error)
	return DefinitionSection::createSection(context, line);
}
