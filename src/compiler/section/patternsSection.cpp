#include "patternsSection.h"
#include "parseContext.h"

bool PatternsSection::processLine(ParseContext &, CodeLine *line) {
	// directly add this line as pattern definition
	parent->patternDefinitions.push_back(new PatternDefinition(Range(line, line->patternText), parent));
	line->resolved = true;
	return true;
}

Section *PatternsSection::createSection(ParseContext &context, CodeLine *line) {
	context.addDiagnostic(Diagnostic(
		context, Diagnostic::Level::Error, "patterns section cannot create sections", Range(line, line->patternText),
		"section_type", sectionTypeToString(type)
	));
	return nullptr;
}
