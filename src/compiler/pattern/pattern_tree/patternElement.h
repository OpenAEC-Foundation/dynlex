#pragma once
#include <string>
#include <vector>
struct PatternElement
{
	enum Type
	{
		// anything not in [A-Za-z0-9]+
		// examples: ' ', '%'
		Other,
		// any string looking like a variable: [A-Za-z0-9]+
		// examples: 'the', 'or'
		VariableLike,
		// a variable
		Variable,
		Count
	};
	Type type;
	// for example: 'the'
	std::string text;
	// position relative to pattern start
	size_t startPos{};
	PatternElement(Type type, std::string text = {}, size_t startPos = {}) : type(type), text(text), startPos(startPos) {}
};

std::vector<PatternElement> getPatternElements(std::string_view patternString);