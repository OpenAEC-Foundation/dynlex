#include "classSection.h"
#include "membersSection.h"
#include "parseContext.h"
#include "parseUtils.h"
#include "patternsSection.h"

bool ClassSection::processLine(ParseContext &context, CodeLine *line) {
	// Inline members: "members: x, y, z" or "members: x as i32, y as i32"
	std::string_view text = line->patternText;
	if (text.starts_with("members: ") || text.starts_with("members:")) {
		std::string_view fields = text.substr(text.find(':') + 1);
		bool success = true;
		parseCommaSeparatedList(fields, [&](std::string_view fieldText) {
			if (!parseFieldDeclaration(context, fieldText, line, this)) {
				success = false;
			}
		});
		line->resolved = true;
		return true;
	}

	// Inline alignment: "alignment: N"
	if (text.starts_with("alignment:")) {
		size_t colonPos = text.find(':');
		std::string_view numStr = text.substr(colonPos + 1);
		size_t start = numStr.find_first_not_of(" \t");
		if (start != std::string_view::npos)
			numStr = numStr.substr(start);
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
