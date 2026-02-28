#include "classSection.h"
#include "membersSection.h"
#include "parseContext.h"
#include "parseUtils.h"
#include "patternsSection.h"

bool ClassSection::processLine(ParseContext &context, CodeLine *line) {
	// Inline members: "members: x, y, z" or "members: x as i32, y as i32"
	std::string_view text = line->patternText;
	if (text.starts_with("members: ") || text.starts_with("members:")) {
		size_t colonPos = text.find(':');
		context.addSourceToken(Range(line, text.substr(0, colonPos + 1)), ParseContext::SourceTokenKind::Keyword);
		std::string_view fields = text.substr(colonPos + 1);
		bool success = true;
		parseCommaSeparatedListWithRanges(fields, [&](std::string_view /*fieldText*/, size_t start, size_t end) {
			if (!parseFieldDeclaration(context, Range(line, text.substr(colonPos + 1 + start, end - start)), this)) {
				success = false;
			}
		});
		line->resolved = true;
		return success;
	}

	// Inline alignment: "alignment: N"
	if (text.starts_with("alignment:")) {
		size_t colonPos = text.find(':');
		std::string_view numStr = text.substr(colonPos + 1);
		size_t start = numStr.find_first_not_of(" \t");
		if (start != std::string_view::npos)
			numStr = numStr.substr(start);
		context.addSourceToken(Range(line, text.substr(0, colonPos + 1)), ParseContext::SourceTokenKind::Keyword);
		if (!numStr.empty())
			context.addSourceToken(Range(line, numStr), ParseContext::SourceTokenKind::Number);
		classDefinition->alignment = std::stoi(std::string(numStr));
		line->resolved = true;
		return true;
	}

	context.diagnostics.push_back(
		Diagnostic(Diagnostic::Level::Error, "unexpected line in class definition", Range(line, line->patternText))
	);
	return false;
}

Section *ClassSection::createSection(ParseContext &context, CodeLine *line) {
	if (line->patternText == "patterns") {
		return new PatternsSection(this);
	}
	if (line->patternText == "members") {
		return new MembersSection(this);
	}

	// Fall back to base class (handles "replacement" for macros, or gives error)
	return DefinitionSection::createSection(context, line);
}
