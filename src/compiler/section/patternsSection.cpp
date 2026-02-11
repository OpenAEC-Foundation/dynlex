#include "patternsSection.h"
#include "parseContext.h"

bool PatternsSection::processLine(ParseContext &, CodeLine *line) {
	// directly add this line as pattern definition
	parent->patternDefinitions.push_back(new PatternDefinition(Range(line, line->patternText), parent));
	line->resolved = true;
	return true;
}

Section *PatternsSection::createSection(ParseContext &context, CodeLine *line) {
	context.diagnostics.push_back(Diagnostic(
		Diagnostic::Level::Error, "you can't create sections in a " + sectionTypeToString(type) + " section",
		Range(line, line->patternText)
	));
	return nullptr;
}
