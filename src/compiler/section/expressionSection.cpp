#include "expressionSection.h"
#include "parseContext.h"

bool ExpressionSection::processLine(ParseContext &context, CodeLine *line) {
	context.diagnostics.push_back(
		Diagnostic(Diagnostic::Level::Error, "Code without get: section", Range(line, line->patternText))
	);
	return false;
}

Section *ExpressionSection::createSection(ParseContext &context, CodeLine *line) {
	if (line->patternText == "get") {
		return new Section(SectionType::Custom, this);
	} else {
		context.diagnostics.push_back(Diagnostic(
			Diagnostic::Level::Error, "Unknown section:" + (std::string)line->patternText, Range(line, line->patternText)
		));
		return nullptr;
	}
}
