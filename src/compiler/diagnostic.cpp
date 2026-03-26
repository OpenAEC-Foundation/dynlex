#include "diagnostic.h"

Diagnostic Diagnostic::configParseError(std::string_view diagnosticMessage, Range diagnosticRange) {
	Diagnostic diagnostic;
	diagnostic.level = Diagnostic::Level::Error;
	diagnostic.message = std::string(diagnosticMessage);
	diagnostic.range = diagnosticRange;
	return diagnostic;
}

std::string Diagnostic::toString() const {
	std::string result = range.toString() + ": " + enumToString(level) + ": " + message;
	for (const auto &related : relatedInfo)
		result += "\n  note: " + related.message + " " + related.range.toString();
	return result;
}
