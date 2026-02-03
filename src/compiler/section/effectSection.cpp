#include "effectSection.h"
#include "parseContext.h"

Section *EffectSection::createSection(ParseContext &context, CodeLine *line) {
	// EffectSection uses "execute" for its content
	if (line->patternText == "execute") {
		return new Section(SectionType::Execute, this);
	}

	// Fall back to base class (handles "replacement" for macros, or gives error)
	return DefinitionSection::createSection(context, line);
}
