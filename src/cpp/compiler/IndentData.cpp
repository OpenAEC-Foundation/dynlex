#include "IndentData.h"
using namespace std::literals;

IndentMeasurement measureIndent(IndentData &data, std::string_view indentString) {
	IndentMeasurement result;
	if (data.indentString.empty()) {
		data.indentString = indentString;
		result.physicalIndentLevel = !indentString.empty();
	} else if (indentString.length() % data.indentString.length() != 0) {
		result.validAmount = false;
	}

	if (indentString.empty()) {
		data.indentString.clear();
		return result;
	}

	char expectedIndentCharacter = data.indentString[0];
	result.invalidCharacterIndex = indentString.find_first_not_of(expectedIndentCharacter);
	if (result.invalidCharacterIndex == std::string_view::npos)
		result.physicalIndentLevel = static_cast<int>(indentString.length() / data.indentString.length());
	return result;
}

std::string charName(char c) {
	switch (c) {
	case ' ':
		return "space";
	case '\t':
		return "tab";
	default:
		return "'"s + c + "'";
	}
}
