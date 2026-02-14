#include "definitionSection.h"
#include "diagnostic.h"
#include "globalsSection.h"
#include "parseContext.h"
#include "parseUtils.h"
#include "patternsSection.h"

bool DefinitionSection::processLine(ParseContext &context, CodeLine *line) {
	// Handle inline globals: "globals: var1, var2, var3"
	std::string_view text = line->patternText;
	if (text.starts_with("globals: ") || text.starts_with("globals:")) {
		std::string_view vars = text.substr(text.find(':') + 1);
		parseCommaSeparatedList(vars, [&](std::string_view varName) {
			std::string varNameStr(varName);
			globalVariables.push_back(varNameStr);
			context.declaredGlobalVariables.insert(varNameStr);
		});
		line->resolved = true;
		return true;
	}

	context.diagnostics.push_back(
		Diagnostic(Diagnostic::Level::Error, "Code without body section", Range(line, line->patternText))
	);
	return false;
}

Section *DefinitionSection::createSection(ParseContext &context, CodeLine *line) {
	// Macros use "replacement", handled here in base class
	if (isMacro && line->patternText == "replacement") {
		return new Section(SectionType::Replacement, this);
	}

	if (line->patternText == "patterns") {
		return new PatternsSection(this);
	}

	if (line->patternText == "globals") {
		return new GlobalsSection(this);
	}

	// Nothing matched - give error
	context.diagnostics.push_back(Diagnostic(
		Diagnostic::Level::Error, "Unknown section: " + (std::string)line->patternText, Range(line, line->patternText)
	));
	return nullptr;
}
