#include "patternElement.h"
#include "transformedPattern.h"
#include <cassert>
#include <regex>
using namespace std::literals;

std::vector<PatternElement> getPatternElements(std::string_view patternString) {
	std::vector<PatternElement> elements{};

	if (patternString.empty())
		return elements;

	PatternElement::Type currentType = PatternElement::Type::Count;

	const char *currentStart = nullptr;
	const char *it;
	for (it = patternString.begin(); it != patternString.end(); it++) {
		PatternElement::Type newType = *it == argumentChar								? PatternElement::Type::Variable
									   : std::regex_match(""s + *it, std::regex("\\w")) ? PatternElement::Type::VariableLike
																						: PatternElement::Type::Other;
		if (newType != currentType) {
			if (currentStart) {
				elements.push_back(
					PatternElement(currentType, std::string(currentStart, it), currentStart - patternString.begin())
				);
			}
			currentStart = it;
			currentType = newType;
		}
	}
	elements.push_back(PatternElement(currentType, std::string(currentStart, it), currentStart - patternString.begin()));

	return elements;
}

std::vector<PatternElement> parsePatternElements(std::string_view patternString, size_t offset) {
	std::vector<PatternElement> result;
	size_t pos = 0;

	while (pos < patternString.size()) {
		// find the next '[' or '{'
		size_t choiceStart = patternString.find('[', pos);
		size_t curlyStart = patternString.find('{', pos);
		size_t bracketStart = std::min(choiceStart, curlyStart);

		if (bracketStart == std::string_view::npos) {
			// no more brackets - parse remaining plain text
			auto plain = getPatternElements(patternString.substr(pos));
			for (auto &elem : plain)
				elem.startPos += pos + offset;
			result.insert(result.end(), plain.begin(), plain.end());
			break;
		}

		// parse plain text before the bracket
		if (bracketStart > pos) {
			auto plain = getPatternElements(patternString.substr(pos, bracketStart - pos));
			for (auto &elem : plain)
				elem.startPos += pos + offset;
			result.insert(result.end(), plain.begin(), plain.end());
		}

		bool isCurly = (bracketStart == curlyStart);
		char openBracket = isCurly ? '{' : '[';
		char closeBracket = isCurly ? '}' : ']';

		// find matching close bracket
		size_t depth = 1;
		size_t i = bracketStart + 1;
		while (i < patternString.size() && depth > 0) {
			if (patternString[i] == openBracket)
				depth++;
			else if (patternString[i] == closeBracket)
				depth--;
			i++;
		}
		assert(depth == 0); // unmatched bracket

		// extract content between brackets
		std::string_view content = patternString.substr(bracketStart + 1, i - bracketStart - 2);

		if (isCurly) {
			// {type:name} — capture element
			size_t colonPos = content.find(':');
			assert(colonPos != std::string_view::npos && "Capture element must have format {type:name}");
			std::string_view captureType = content.substr(0, colonPos);
			std::string name(content.substr(colonPos + 1));
			if (captureType == "word") {
				result.push_back(PatternElement(PatternElement::Type::Word, name, bracketStart + offset));
			} else {
				assert(false && "Unknown capture type");
			}
		} else {
			// [alternative1|alternative2|...] — choice element

			// split by '|' at top level (not inside nested brackets)
			std::vector<std::string_view> parts;
			size_t partStart = 0;
			depth = 0;
			for (size_t j = 0; j < content.size(); j++) {
				if (content[j] == '[')
					depth++;
				else if (content[j] == ']')
					depth--;
				else if (content[j] == '|' && depth == 0) {
					parts.push_back(content.substr(partStart, j - partStart));
					partStart = j + 1;
				}
			}
			parts.push_back(content.substr(partStart));

			// create Choice element with alternatives
			PatternElement choice(PatternElement::Type::Choice, {}, bracketStart + offset);
			size_t altOffset = bracketStart + 1 + offset;
			for (auto &part : parts) {
				choice.alternatives.push_back(parsePatternElements(part, altOffset));
				altOffset += part.size() + 1; // +1 for '|'
			}
			// if the choice has an empty alternative and is followed by a space,
			// absorb the space into non-empty alternatives to avoid double spaces.
			// e.g. [the|] screen → [the |]screen, so empty matches "screen" not " screen"
			bool hasEmptyAlternative = false;
			for (auto &alt : choice.alternatives) {
				if (alt.empty()) {
					hasEmptyAlternative = true;
					break;
				}
			}
			if (hasEmptyAlternative && i < patternString.size() && patternString[i] == ' ') {
				for (auto &alt : choice.alternatives) {
					if (!alt.empty()) {
						alt.push_back(PatternElement(PatternElement::Type::Other, " ", i + offset));
					}
				}
				i++; // skip the space in the main sequence
			}

			result.push_back(std::move(choice));
		}

		pos = i; // continue after closing bracket
	}

	return result;
}
