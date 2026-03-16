#include "definitionSection.h"
#include "diagnostic.h"
#include "globalsSection.h"
#include "parseContext.h"
#include "parseUtils.h"
#include "patternsSection.h"
#include "precedenceSection.h"
#include "replacementSection.h"
#include "syntaxConfig.h"

bool DefinitionSection::processLine(ParseContext &context, CodeLine *line) {
	const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
	// Handle inline globals: "globals: var1, var2, var3"
	std::string_view text = line->patternText;
	if (std::optional<std::string_view> varsMatch =
			extractInlineSettingValue(text, syntax.globalsSectionName, syntax.sectionOpener)) {
		std::string_view vars = *varsMatch;
		size_t valueStart = text.size() - vars.size();
		context.addSourceToken(Range(line, text.substr(0, valueStart)), ParseContext::SourceTokenKind::Keyword);
		parseCommaSeparatedListWithRanges(vars, [&](std::string_view varName, size_t start, size_t end) {
			std::string varNameStr(varName);
			globalVariables.push_back(varNameStr);
			context.declaredGlobalVariables.insert(varNameStr);
			context.addSourceToken(
				Range(line, text.substr(valueStart + start, end - start)), ParseContext::SourceTokenKind::Variable
			);
		});
		line->resolved = true;
		return true;
	}

	// Handle inline before: "before: $ + $, $ - $"
	if (std::optional<std::string_view> patternsMatch =
			extractInlineSettingValue(text, syntax.beforeSectionName, syntax.sectionOpener)) {
		std::string_view patterns = *patternsMatch;
		size_t valueStart = text.size() - patterns.size();
		context.addSourceToken(Range(line, text.substr(0, valueStart)), ParseContext::SourceTokenKind::Keyword);
		parseCommaSeparatedListWithRanges(patterns, [&](std::string_view pattern, size_t start, size_t end) {
			beforePatterns.push_back(std::string(pattern));
			Range patternRange(line, text.substr(valueStart + start, end - start));
			context.addSourceToken(
				patternRange,
				pattern == syntax.precedenceDefaultName ? ParseContext::SourceTokenKind::Keyword
														: ParseContext::SourceTokenKind::PatternReference,
				SectionType::Function
			);
		}, [&](std::string_view /*separator*/, size_t start, size_t end) {
			context.addSourceToken(
				Range(line, text.substr(valueStart + start, end - start)), ParseContext::SourceTokenKind::PatternReference,
				SectionType::Function
			);
		});
		line->resolved = true;
		return true;
	}

	// Handle inline after: "after: $ + $, $ - $"
	if (std::optional<std::string_view> patternsMatch =
			extractInlineSettingValue(text, syntax.afterSectionName, syntax.sectionOpener)) {
		std::string_view patterns = *patternsMatch;
		size_t valueStart = text.size() - patterns.size();
		context.addSourceToken(Range(line, text.substr(0, valueStart)), ParseContext::SourceTokenKind::Keyword);
		parseCommaSeparatedListWithRanges(patterns, [&](std::string_view pattern, size_t start, size_t end) {
			afterPatterns.push_back(std::string(pattern));
			Range patternRange(line, text.substr(valueStart + start, end - start));
			context.addSourceToken(
				patternRange,
				pattern == syntax.precedenceDefaultName ? ParseContext::SourceTokenKind::Keyword
														: ParseContext::SourceTokenKind::PatternReference,
				SectionType::Function
			);
		}, [&](std::string_view /*separator*/, size_t start, size_t end) {
			context.addSourceToken(
				Range(line, text.substr(valueStart + start, end - start)), ParseContext::SourceTokenKind::PatternReference,
				SectionType::Function
			);
		});
		line->resolved = true;
		return true;
	}

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
