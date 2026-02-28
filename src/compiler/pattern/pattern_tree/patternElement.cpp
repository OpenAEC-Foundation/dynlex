#include "patternElement.h"
#include "transformedPattern.h"
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
		// Split Other-type sequences at space boundaries so spaces are always separate elements.
		// Without this, "% " would merge into one element, preventing sub-expression matching
		// (e.g. "10%" where "%" is part of an expression pattern but followed by a space).
		bool splitAtSpace =
			(newType == PatternElement::Type::Other && currentType == PatternElement::Type::Other &&
			 ((*it == ' ') != (*currentStart == ' ')));
		if (newType != currentType || splitAtSpace) {
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

// Absorb VariableLike elements adjacent to Choice elements into the Choice alternatives.
// e.g., VL("te") + Choice([VL("st")], []) → Choice([VL("test")], [VL("te")])
// This ensures structurally different but semantically equivalent patterns produce the same trie paths.
// Only VariableLike elements are absorbed: a merged VariableLike (e.g. "test" from "te"+"st") can never
// be a variable since variables are single standalone words, so this is always safe.
static void normalizePatternElements(std::vector<DefinitionPatternElement> &elements) {
	// Recursively normalize inside Choice alternatives first
	for (auto &elem : elements) {
		if (elem.type == PatternElement::Type::Choice) {
			for (auto &alt : elem.alternatives)
				normalizePatternElements(alt);
		}
	}

	// Repeatedly absorb adjacent VariableLike elements into choices until no more changes
	bool changed = true;
	while (changed) {
		changed = false;
		for (size_t i = 0; i < elements.size(); i++) {
			if (elements[i].type != PatternElement::Type::Choice)
				continue;

			// Absorb preceding VariableLike element
			if (i > 0 && elements[i - 1].type == PatternElement::Type::VariableLike) {
				auto &prev = elements[i - 1];
				for (auto &alt : elements[i].alternatives) {
					if (alt.empty() || alt.front().type != PatternElement::Type::VariableLike) {
						alt.insert(alt.begin(), prev);
					} else {
						alt.front().text = prev.text + alt.front().text;
						alt.front().startPos = prev.startPos;
					}
				}
				elements.erase(elements.begin() + (i - 1));
				changed = true;
				break;
			}

			// Absorb following VariableLike element
			if (i + 1 < elements.size() && elements[i + 1].type == PatternElement::Type::VariableLike) {
				auto &next = elements[i + 1];
				for (auto &alt : elements[i].alternatives) {
					if (alt.empty() || alt.back().type != PatternElement::Type::VariableLike) {
						alt.push_back(next);
					} else {
						alt.back().text += next.text;
					}
				}
				elements.erase(elements.begin() + (i + 1));
				changed = true;
				break;
			}
		}
	}
}

static std::vector<DefinitionPatternElement>
parsePatternFailure(std::string *errorMessage, size_t *errorOffset, std::string message, size_t offset) {
	if (errorMessage)
		*errorMessage = std::move(message);
	if (errorOffset)
		*errorOffset = offset;
	return {};
}

std::vector<DefinitionPatternElement>
parsePatternElements(std::string_view patternString, size_t offset, std::string *errorMessage, size_t *errorOffset) {
	std::vector<DefinitionPatternElement> result;
	size_t pos = 0;

	while (pos < patternString.size()) {
		// find the next '[' or '{'
		size_t choiceStart = patternString.find('[', pos);
		size_t curlyStart = patternString.find('{', pos);
		size_t bracketStart = std::min(choiceStart, curlyStart);

		if (bracketStart == std::string_view::npos) {
			// no more brackets - parse remaining plain text
			auto plain = getPatternElements(patternString.substr(pos));
			for (auto &elem : plain) {
				elem.startPos += pos + offset;
				result.push_back(elem);
			}
			break;
		}

		// parse plain text before the bracket
		if (bracketStart > pos) {
			auto plain = getPatternElements(patternString.substr(pos, bracketStart - pos));
			for (auto &elem : plain) {
				elem.startPos += pos + offset;
				result.push_back(elem);
			}
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
		if (depth != 0) {
			return parsePatternFailure(
				errorMessage, errorOffset, std::string("Unmatched '") + openBracket + "' in pattern", bracketStart + offset
			);
		}

		// extract content between brackets
		std::string_view content = patternString.substr(bracketStart + 1, i - bracketStart - 2);

		if (isCurly) {
			// {type:name} — capture element
			size_t colonPos = content.find(':');
			if (colonPos == std::string_view::npos) {
				return parsePatternFailure(
					errorMessage, errorOffset, "Capture element must have format {type:name}", bracketStart + offset
				);
			}
			std::string_view captureType = content.substr(0, colonPos);
			std::string name(content.substr(colonPos + 1));
			size_t namePos = bracketStart + 1 + colonPos + 1 + offset;
			if (captureType == "word") {
				result.push_back(DefinitionPatternElement(PatternElement::Type::Word, name, namePos));
			} else {
				// Typed argument constraint: emit a Variable element with the type constraint
				DefinitionPatternElement elem(PatternElement::Type::Variable, name, namePos);
				elem.typeConstraintName = std::string(captureType);
				result.push_back(std::move(elem));
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
			DefinitionPatternElement choice(PatternElement::Type::Choice, {}, bracketStart + offset);
			size_t altOffset = bracketStart + 1 + offset;
			for (auto &part : parts) {
				std::vector<DefinitionPatternElement> alternative =
					parsePatternElements(part, altOffset, errorMessage, errorOffset);
				if (errorMessage && !errorMessage->empty())
					return {};
				choice.alternatives.push_back(std::move(alternative));
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
						alt.push_back(DefinitionPatternElement(PatternElement::Type::Other, " ", i + offset));
					}
				}
				i++; // skip the space in the main sequence
			}

			result.push_back(std::move(choice));
		}

		pos = i; // continue after closing bracket
	}

	normalizePatternElements(result);
	return result;
}
