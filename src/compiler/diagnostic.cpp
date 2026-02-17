#include "diagnostic.h"

std::string Diagnostic::toString() const {
	std::string result = range.toString() + ": " + enumToString(level) + ": " + message;
	for (const auto &related : relatedInfo) {
		result += " (" + related.message + " " + related.range.toString() + ")";
	}
	return result + " ";
}
