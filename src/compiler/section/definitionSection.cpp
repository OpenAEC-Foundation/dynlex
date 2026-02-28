#include "definitionSection.h"
#include "diagnostic.h"
#include "globalsSection.h"
#include "parseContext.h"
#include "parseUtils.h"
#include "patternsSection.h"
#include "precedenceSection.h"
#include "replacementSection.h"

bool DefinitionSection::processLine(ParseContext &context, CodeLine *line) {
	// Handle inline globals: "globals: var1, var2, var3"
	std::string_view text = line->patternText;
	if (text.starts_with("globals: ") || text.starts_with("globals:")) {
		size_t colonPos = text.find(':');
		context.addSourceToken(Range(line, text.substr(0, colonPos + 1)), ParseContext::SourceTokenKind::Keyword);
		std::string_view vars = text.substr(colonPos + 1);
		parseCommaSeparatedListWithRanges(vars, [&](std::string_view varName, size_t start, size_t end) {
			std::string varNameStr(varName);
			globalVariables.push_back(varNameStr);
			context.declaredGlobalVariables.insert(varNameStr);
			context.addSourceToken(
				Range(line, text.substr(colonPos + 1 + start, end - start)), ParseContext::SourceTokenKind::Variable
			);
		});
		line->resolved = true;
		return true;
	}

	// Handle inline before: "before: $ + $, $ - $"
	if (text.starts_with("before: ") || text.starts_with("before:")) {
		size_t colonPos = text.find(':');
		context.addSourceToken(Range(line, text.substr(0, colonPos + 1)), ParseContext::SourceTokenKind::Keyword);
		std::string_view patterns = text.substr(colonPos + 1);
		parseCommaSeparatedListWithRanges(patterns, [&](std::string_view pattern, size_t start, size_t end) {
			beforePatterns.push_back(std::string(pattern));
			Range patternRange(line, text.substr(colonPos + 1 + start, end - start));
			context.addSourceToken(
				patternRange,
				pattern == "default" ? ParseContext::SourceTokenKind::Keyword : ParseContext::SourceTokenKind::PatternReference,
				SectionType::Expression
			);
		}, [&](std::string_view /*separator*/, size_t start, size_t end) {
			context.addSourceToken(
				Range(line, text.substr(colonPos + 1 + start, end - start)), ParseContext::SourceTokenKind::PatternReference,
				SectionType::Expression
			);
		});
		line->resolved = true;
		return true;
	}

	// Handle inline after: "after: $ + $, $ - $"
	if (text.starts_with("after: ") || text.starts_with("after:")) {
		size_t colonPos = text.find(':');
		context.addSourceToken(Range(line, text.substr(0, colonPos + 1)), ParseContext::SourceTokenKind::Keyword);
		std::string_view patterns = text.substr(colonPos + 1);
		parseCommaSeparatedListWithRanges(patterns, [&](std::string_view pattern, size_t start, size_t end) {
			afterPatterns.push_back(std::string(pattern));
			Range patternRange(line, text.substr(colonPos + 1 + start, end - start));
			context.addSourceToken(
				patternRange,
				pattern == "default" ? ParseContext::SourceTokenKind::Keyword : ParseContext::SourceTokenKind::PatternReference,
				SectionType::Expression
			);
		}, [&](std::string_view /*separator*/, size_t start, size_t end) {
			context.addSourceToken(
				Range(line, text.substr(colonPos + 1 + start, end - start)), ParseContext::SourceTokenKind::PatternReference,
				SectionType::Expression
			);
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
		return executionSection = new ReplacementSection(this);
	}

	if (line->patternText == "execute") {
		return executionSection = new Section(SectionType::Get, this);
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
