#include "classSection.h"
#include "alignmentSection.h"
#include "membersSection.h"
#include "parseContext.h"
#include "patternsSection.h"
#include "syntaxConfig.h"

bool ClassSection::processLine(ParseContext &context, CodeLine *line) {
	context.addDiagnostic(Diagnostic(context, Diagnostic::Level::Error, "unexpected class line", Range(line, line->patternText))
	);
	return false;
}

Section *ClassSection::createSection(ParseContext &context, CodeLine *line) {
	const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
	if (matchesConfiguredKeyword(line->patternText, syntax.patternsSectionName)) {
		return new PatternsSection(this);
	}
	if (matchesConfiguredKeyword(line->patternText, syntax.membersSectionName)) {
		return new MembersSection(this);
	}
	if (matchesConfiguredKeyword(line->patternText, syntax.alignmentName)) {
		return new AlignmentSection(this);
	}

	// Fall back to base class (handles "replacement" for macros, or gives error)
	return DefinitionSection::createSection(context, line);
}
