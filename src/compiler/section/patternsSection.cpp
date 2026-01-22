#include "patternsSection.h"
#include "parseContext.h"

bool PatternsSection::processLine(ParseContext &, CodeLine *line) {
	// directly add this line as pattern definition
	parent->patternDefinitions.push_back(new PatternDefinition(Range(line, line->fullText)));
	return true;
}

Section *PatternsSection::createSection(ParseContext &context, CodeLine *line) {
	context.diagnostics.push_back(Diagnostic(
		Diagnostic::Level::Error, "you can't create sections in a " + enumToString(type) + " section",
		Range(line, line->fullText)
	));
	return nullptr;
}
