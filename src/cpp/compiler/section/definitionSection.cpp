#include "definitionSection.h"
#include "diagnostic.h"
#include "globalsSection.h"
#include "parseContext.h"
#include "patternsSection.h"
#include "precedenceSection.h"
#include "replacementSection.h"
#include "syntaxConfig.h"

bool DefinitionSection::processLine(ParseContext &context, CodeLine *line) {
	context.diagnostics.push_back(
		Diagnostic(context, Diagnostic::Level::Error, "missing body section", Range(line, line->patternText))
	);
	return false;
}

Section *DefinitionSection::createSection(ParseContext &context, CodeLine *line) {
	const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
	// replacement: implies macro semantics for this definition.
	if (matchesConfiguredKeyword(line->patternText, syntax.replacementSectionName)) {
		isMacro = true;
		return executionSection = new ReplacementSection(this);
	}

	if (matchesConfiguredKeyword(line->patternText, syntax.executeSectionName)) {
		return executionSection = new Section(SectionType::Get, this);
	}

	if (matchesConfiguredKeyword(line->patternText, syntax.patternsSectionName)) {
		return new PatternsSection(this);
	}

	if (matchesConfiguredKeyword(line->patternText, syntax.globalsSectionName)) {
		return new GlobalsSection(this);
	}

	// Nothing matched - give error
	context.addDiagnostic(Diagnostic(
		context, Diagnostic::Level::Error, "unknown section", Range(line, line->patternText), "section", line->patternText
	));
	return nullptr;
}
