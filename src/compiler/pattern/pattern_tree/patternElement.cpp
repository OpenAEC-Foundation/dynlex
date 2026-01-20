#include "patternElement.h"
#include "transformedPattern.h"
#include <regex>
using namespace std::literals;

std::vector<PatternElement> getPatternElements(std::string_view patternString) {
	// std::regex variableRegex{"\\w+"};
	// std::regex argumentRegex{"\\a"};

	// split the pattern in variable and non-variable like patterns
	// std::cregex_token_iterator iter(patternString.begin(), patternString.end(), variableRegex);
	// std::cregex_token_iterator end;

	// std::size_t otherStart = 0;
	std::vector<PatternElement> elements{};

	PatternElement::Type currentType = PatternElement::Type::Count;

	const char *currentStart = nullptr;
	const char *it;
	for (it = patternString.begin(); it != patternString.end(); it++) {
		PatternElement::Type newType = *it == argumentChar								? PatternElement::Type::Variable
									   : std::regex_match(""s + *it, std::regex("\\w")) ? PatternElement::Type::VariableLike
																						: PatternElement::Type::Other;
		if (newType != currentType) {
			if (currentStart) {
				elements.push_back(PatternElement(currentType, std::string(currentStart, it), currentStart - patternString.begin()));
			}
			currentStart = it;
			currentType = newType;
		}
	}
	elements.push_back(PatternElement(currentType, std::string(currentStart, it), currentStart - patternString.begin()));

	// auto addElement = [&elements](PatternElement::Type type, std::string_view str) {
	//	if (str.length()) {
	//		elements.push_back(PatternElement(type, (std::string)str));
	//	}
	// };
	//// since there are only two types of elements, other and variable like tokens follow eachother.
	//// we parse like this:
	//// other var other var other
	//// the first and last other will get omitted if empty.
	// for (; iter != end; ++iter) {
	//	std::size_t variableStart = iter->first - patternString.begin();
	//
	//	addElement(PatternElement::Type::Other, patternString.substr(otherStart, variableStart - otherStart));
	//	addElement(PatternElement::Type::VariableLike, patternString.substr(variableStart, iter->length()));
	//	otherStart = variableStart + iter->length();
	//}
	// addElement(PatternElement::Type::Other, patternString.substr(otherStart));

	return elements;
}