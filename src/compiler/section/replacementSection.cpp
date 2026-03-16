#include "replacementSection.h"

bool ReplacementSection::processLine(ParseContext &context, CodeLine *line) {
	line->expression = detectPatterns(context, Range(line, line->patternText), SectionType::Function);
	return line->expression != nullptr;
}
