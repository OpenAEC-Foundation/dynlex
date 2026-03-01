#include "functionSection.h"
#include "parseContext.h"
#include "precedenceSection.h"
#include "syntaxConfig.h"

Section *FunctionSection::createSection(ParseContext &context, CodeLine *line) {
	const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);

	if (matchesConfiguredKeyword(line->patternText, syntax.beforeSectionName)) {
		return new PrecedenceSection(SectionType::Before, this);
	}

	if (matchesConfiguredKeyword(line->patternText, syntax.afterSectionName)) {
		return new PrecedenceSection(SectionType::After, this);
	}
	// Fall back to base class (handles "replacement" for macros, or gives error)
	return DefinitionSection::createSection(context, line);
}
