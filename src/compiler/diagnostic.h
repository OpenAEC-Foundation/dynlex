#pragma once
#include "range.h"
#include "sourceFile.h"
#include "stringFunctions.h"
#include <string>
#include <string_view>

struct Diagnostic {
	enum class Level { Info, Warning, Error };
	Level level;
	std::string message;
	Range range;
	Diagnostic(Level level, std::string message, Range range) : level(level), message(message), range(range) {}
	std::string toString() const;
};

template <> inline bool stringToEnum<Diagnostic::Level>(std::string_view levelName, Diagnostic::Level &result) {
	if (levelName == "Info") {
		result = Diagnostic::Level::Info;
		return true;
	}
	if (levelName == "Warning") {
		result = Diagnostic::Level::Warning;
		return true;
	}
	if (levelName == "Error") {
		result = Diagnostic::Level::Error;
		return true;
	}
	return false;
}
template <> inline bool enumToString<Diagnostic::Level>(Diagnostic::Level level, std::string &result) {
	switch (level) {
	case Diagnostic::Level::Info:
		result = "Info";
		return true;
	case Diagnostic::Level::Warning:
		result = "Warning";
		return true;
	case Diagnostic::Level::Error:
		result = "Error";
		return true;
	}
	return false;
}
