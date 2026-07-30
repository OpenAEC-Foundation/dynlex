#include "diagnostic.h"

Diagnostic unknownTypeConstraintDiagnostic(
	const ParseContext &context, Range range, std::string_view constraint
) {
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
	std::string result = range.toString() + ": " + enumToString(level) + ": " + message;
	for (const auto &related : relatedInfo)
		result += "\n  note: " + related.message + " " + related.range.toString();
	return result;
}
