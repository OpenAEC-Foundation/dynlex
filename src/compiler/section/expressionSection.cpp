#include "expressionSection.h"
#include "parseContext.h"
#include "precedenceSection.h"

Section *ExpressionSection::createSection(ParseContext &context, CodeLine *line) {

	if (line->patternText == "before") {
		return new PrecedenceSection(SectionType::Before, this);
	}

	if (line->patternText == "after") {
		return new PrecedenceSection(SectionType::After, this);
	}
	// Fall back to base class (handles "replacement" for macros, or gives error)
	return DefinitionSection::createSection(context, line);
}
