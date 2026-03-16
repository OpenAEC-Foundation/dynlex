#include "classSection.h"
#include "membersSection.h"
#include "parseContext.h"
#include "parseUtils.h"
#include "patternsSection.h"
#include "syntaxConfig.h"

bool ClassSection::processLine(ParseContext &context, CodeLine *line) {
	const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
	// Inline members: "members: x, y, z" or "members: x as i32, y as i32"
	std::string_view text = line->patternText;
	if (std::optional<std::string_view> fieldsMatch =
			extractInlineSettingValue(text, syntax.membersSectionName, syntax.sectionOpener)) {
		std::string_view fields = *fieldsMatch;
		size_t valueStart = text.size() - fields.size();
		context.addSourceToken(Range(line, text.substr(0, valueStart)), ParseContext::SourceTokenKind::Keyword);
		bool success = true;
		parseCommaSeparatedListWithRanges(fields, [&](std::string_view /*fieldText*/, size_t start, size_t end) {
			if (!parseFieldDeclaration(context, Range(line, text.substr(valueStart + start, end - start)), this)) {
				success = false;
			}
		});
		line->resolved = true;
		return success;
	}

	// Inline alignment: "alignment: N"
	if (std::optional<std::string_view> numStrMatch =
			extractInlineSettingValue(text, syntax.alignmentName, syntax.sectionOpener)) {
		std::string_view numStr = *numStrMatch;
		size_t start = numStr.find_first_not_of(" \t");
		if (start != std::string_view::npos)
			numStr = numStr.substr(start);
		size_t valueStart = text.size() - numStr.size();
		context.addSourceToken(Range(line, text.substr(0, valueStart)), ParseContext::SourceTokenKind::Keyword);
		if (!numStr.empty())
			context.addSourceToken(Range(line, numStr), ParseContext::SourceTokenKind::Number);
		classDefinition->alignment = std::stoi(std::string(numStr));
		line->resolved = true;
		return true;
	}

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

	// Fall back to base class (handles "replacement" for macros, or gives error)
	return DefinitionSection::createSection(context, line);
}
