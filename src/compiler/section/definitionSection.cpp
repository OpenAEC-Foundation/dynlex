#include "definitionSection.h"
#include "diagnostic.h"
#include "parseContext.h"

bool DefinitionSection::processLine(ParseContext &context, CodeLine *line) {
	context.diagnostics.push_back(
		Diagnostic(Diagnostic::Level::Error, "Code without body section", Range(line, line->patternText))
	);
	return false;
}

Section *DefinitionSection::createSection(ParseContext &context, CodeLine *line) {
	// Macros use "replacement", handled here in base class
	if (isMacro && line->patternText == "replacement") {
		return new Section(SectionType::Custom, this);
	}

	// Nothing matched - give error
	context.diagnostics.push_back(Diagnostic(
		Diagnostic::Level::Error, "Unknown section: " + (std::string)line->patternText, Range(line, line->patternText)
	));
	return nullptr;
}
