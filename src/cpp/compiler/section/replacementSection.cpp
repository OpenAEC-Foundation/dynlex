#include "replacementSection.h"
#include "parseContext.h"

bool ReplacementSection::processLine(ParseContext &context, CodeLine *line) {
	if (context.deferParsedLine && context.deferParsedLine(this, line, SectionType::Function))
		return true;
	line->expression = detectPatterns(context, Range(line, line->patternText), SectionType::Function);
	return line->expression != nullptr;
}
