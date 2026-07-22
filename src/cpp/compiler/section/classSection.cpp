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
	if (matchesConfiguredKeyword(line->patternText, syntax.retainSectionName)) {
		if (classDefinition->retainSection) {
			context.addDiagnostic(Diagnostic(
				context, Diagnostic::Level::Error, "class has more than one retain section", Range(line, line->patternText)
			));
			return nullptr;
		}
		return classDefinition->retainSection = new Section(SectionType::Retain, this);
	}
	if (matchesConfiguredKeyword(line->patternText, syntax.releaseSectionName)) {
		if (classDefinition->releaseSection) {
			context.addDiagnostic(Diagnostic(
				context, Diagnostic::Level::Error, "class has more than one release section", Range(line, line->patternText)
			));
			return nullptr;
		}
		return classDefinition->releaseSection = new Section(SectionType::Release, this);
	}

	// Fall back to base class (handles "replacement" for flexes, or gives error)
	return DefinitionSection::createSection(context, line);
}
