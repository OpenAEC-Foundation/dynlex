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
		Variable
	};
	Type type;
	// for example: 'the'
	std::string text;
	PatternElement(Type type, std::string text = {}) : type(type), text(text) {}
};

std::vector<PatternElement> getPatternElements(std::string_view patternString);