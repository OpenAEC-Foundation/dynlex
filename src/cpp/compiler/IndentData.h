#include <string>
struct IndentData {
	// the string repeating indentlevel times
	std::string indentString{};
	// the indent level expected from the next line
	int indentLevel{};
};

std::string charName(char c);