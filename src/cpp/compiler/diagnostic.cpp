#include "diagnostic.h"

Diagnostic unknownTypeConstraintDiagnostic(const ParseContext &context, Range range, std::string_view constraint) {
	return Diagnostic(context, Diagnostic::Level::Error, "unknown type constraint", range, "type_constraint", constraint);
}

Diagnostic Diagnostic::configParseError(std::string_view diagnosticMessage, Range diagnosticRange) {
	Diagnostic diagnostic;
	diagnostic.level = Diagnostic::Level::Error;
	diagnostic.message = std::string(diagnosticMessage);
	diagnostic.range = diagnosticRange;
	return diagnostic;
}

std::string Diagnostic::toString() const {
	return toString([](std::string &output, const Range &location) {
		output += location.toString();
	});
}

std::string Diagnostic::toString(const std::function<void(std::string &, const Range &)> &appendRange) const {
	std::string result;
	appendRange(result, range);
	result += ": " + enumToString(level) + ": " + message;
	for (const auto &related : relatedInfo) {
		result += "\n  note: " + related.message + " ";
		appendRange(result, related.range);
	}
	return result;
}
