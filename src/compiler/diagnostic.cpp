#include "diagnostic.h"

std::string Diagnostic::toString() const { return range.toString() + ": " + enumToString(level) + ": " + message + " "; }
