#include "replacementSection.h"

bool ReplacementSection::processLine(ParseContext &context, CodeLine *line) {
	line->function = detectPatterns(context, Range(line, line->patternText), SectionType::Function);
	return line->function != nullptr;
}
