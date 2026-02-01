#include "expressionSection.h"
#include "parseContext.h"

Section *ExpressionSection::createSection(ParseContext &context, CodeLine *line) {
	// ExpressionSection uses "get" for its content
	if (line->patternText == "get") {
		return new Section(SectionType::Custom, this);
	}

	// Fall back to base class (handles "replacement" for macros, or gives error)
	return DefinitionSection::createSection(context, line);
}
