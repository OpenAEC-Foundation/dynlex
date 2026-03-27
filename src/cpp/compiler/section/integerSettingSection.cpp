#include "integerSettingSection.h"
#include "diagnostic.h"
#include "parseContext.h"
#include <cctype>
#include <charconv>

bool IntegerSettingSection::processLine(ParseContext &context, CodeLine *line) {
	std::string_view valueText = line->patternText;
	size_t valueStart = 0;
	while (valueStart < valueText.size() && std::isspace(static_cast<unsigned char>(valueText[valueStart])))
		valueStart++;
	valueText.remove_prefix(valueStart);

	if (valueText.empty()) {
		context.addDiagnostic(
			Diagnostic(context, Diagnostic::Level::Error, "expected integer value", Range(line, line->patternText))
		);
		return false;
	}

	int value = 0;
	auto [end, error] = std::from_chars(valueText.data(), valueText.data() + valueText.size(), value);
	if (error != std::errc() || end != valueText.data() + valueText.size()) {
		context.addDiagnostic(Diagnostic(context, Diagnostic::Level::Error, "expected integer value", Range(line, valueText)));
		return false;
	}

	context.addSourceToken(Range(line, valueText), ParseContext::SourceTokenKind::Number);
	line->resolved = true;
	return applyValue(context, line, value);
}

Section *IntegerSettingSection::createSection(ParseContext &context, CodeLine *line) {
	context.addDiagnostic(Diagnostic(
		context, Diagnostic::Level::Error, "integer setting section cannot create sections", Range(line, line->patternText)
	));
	return nullptr;
}
