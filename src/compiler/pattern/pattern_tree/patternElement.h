#pragma once
#include <string>
#include <vector>
struct PatternElement {
	enum Type {
		// anything not in [A-Za-z0-9]+
		// examples: ' ', '%'
		Other,
		// any string looking like a variable: [A-Za-z0-9]+
		// examples: 'the', 'or'
		VariableLike,
		// a variable
		Variable,
		// [alternative1|alternative2|...] — each alternative is a sequence of elements
		Choice,
		Count
	};
	Type type;
	// for example: 'the'
	std::string text;
	// position relative to pattern start
	size_t startPos{};
	// for Choice type: each alternative is a sequence of elements
	std::vector<std::vector<PatternElement>> alternatives;
	PatternElement(Type type, const std::string &text = {}, size_t startPos = {})
		: type(type), text(text), startPos(startPos) {}
};

// Parse plain text (no brackets) into pattern elements
std::vector<PatternElement> getPatternElements(std::string_view patternString);

// Parse pattern text with [bracket|alternatives] into elements (calls getPatternElements for plain segments)
std::vector<PatternElement> parsePatternElements(std::string_view patternString, size_t offset = 0);

// Visit all leaf (non-Choice) elements recursively, including inside Choice alternatives
template <typename F> void forEachLeafElement(std::vector<PatternElement> &elements, F &&callback) {
	for (auto &element : elements) {
		if (element.type == PatternElement::Type::Choice) {
			for (auto &alternative : element.alternatives) {
				forEachLeafElement(alternative, callback);
			}
		} else {
			callback(element);
		}
	}
}
