#include "expressionSection.h"
#include "parseContext.h"

Section *ExpressionSection::createSection(ParseContext &context, CodeLine *line) {
	if (line->patternText == "get" || line->patternText == "execute") {
		return new Section(SectionType::Get, this);
	}

	// Fall back to base class (handles "replacement" for macros, or gives error)
	return DefinitionSection::createSection(context, line);
}
