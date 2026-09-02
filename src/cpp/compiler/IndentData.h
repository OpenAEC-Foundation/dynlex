#pragma once
#include <cstddef>
#include <string>
#include <string_view>

struct IndentData {
	// the string repeating indentlevel times
	std::string indentString{};
	// the indent level expected from the next line
	int indentLevel{};
};

struct IndentMeasurement {
	int physicalIndentLevel{};
	bool validAmount = true;
	size_t invalidCharacterIndex = std::string::npos;
};

IndentMeasurement measureIndent(IndentData &data, std::string_view indentString);
std::string charName(char c);
