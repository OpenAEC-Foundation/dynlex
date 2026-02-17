#include "replacementSection.h"

bool ReplacementSection::processLine(ParseContext &context, CodeLine *line) {
	// Expression macro replacements are expressions (e.g. x + x), not effects.
	// Effect macro replacements are effects (e.g. @intrinsic("store", ...)).
	SectionType detectionType =
		(parent && parent->type == SectionType::Expression) ? SectionType::Expression : SectionType::Effect;
	line->expression = detectPatterns(context, Range(line, line->patternText), detectionType);
	return line->expression != nullptr;
}
