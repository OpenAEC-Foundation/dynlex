#include "patternElement.h"
#include "parseContext.h"
#include "transformedPattern.h"
#include <cctype>
#include <unordered_map>
#include <unordered_set>
using namespace std::literals;

std::vector<PatternElement> getPatternElements(std::string_view patternString) {
	std::vector<PatternElement> elements{};

	if (patternString.empty())
		return elements;

	PatternElement::Type currentType = PatternElement::Type::Count;
	size_t currentStart = std::string_view::npos;
	auto isWordChar = [](char c) {
		return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
	};
	for (size_t i = 0; i < patternString.size(); i++) {
		char currentChar = patternString[i];
		PatternElement::Type newType = currentChar == argumentChar ? PatternElement::Type::Variable
									   : isWordChar(currentChar)   ? PatternElement::Type::VariableLike
																   : PatternElement::Type::Other;
		// Split Other-type sequences at space boundaries so spaces are always separate elements.
		// Without this, "% " would merge into one element, preventing sub-expression matching
		// (e.g. "10%" where "%" is part of an expression pattern but followed by a space).
		bool splitAtSpace =
			(newType == PatternElement::Type::Other && currentType == PatternElement::Type::Other &&
			 currentStart != std::string_view::npos && ((currentChar == ' ') != (patternString[currentStart] == ' ')));
		if (newType != currentType || splitAtSpace) {
			if (currentStart != std::string_view::npos) {
				elements.push_back(
					PatternElement(currentType, std::string(patternString.substr(currentStart, i - currentStart)), currentStart)
				);
			}
			currentStart = i;
			currentType = newType;
		}
	}
	elements.push_back(PatternElement(
		currentType, std::string(patternString.substr(currentStart, patternString.size() - currentStart)), currentStart
	));

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

// Choices multiply into concrete pattern paths (see canonicalPatternPaths).
// Bound the product so a pathological definition is rejected with a
// diagnostic instead of expanding without limit.
constexpr size_t maxPatternChoiceCombinations = 1024;

static size_t countPatternPaths(const std::vector<DefinitionPatternElement> &elements) {
	size_t count = 1;
	for (const DefinitionPatternElement &element : elements) {
		if (element.type != PatternElement::Type::Choice)
			continue;
		size_t alternativePaths = 0;
		for (const auto &alternative : element.alternatives) {
			alternativePaths += countPatternPaths(alternative);
			if (alternativePaths > maxPatternChoiceCombinations)
				return maxPatternChoiceCombinations + 1;
		}
		count *= alternativePaths;
		if (count > maxPatternChoiceCombinations)
			return maxPatternChoiceCombinations + 1;
	}
	return count;
}

static bool emitPatternParseFailure(ParseContext &context, Diagnostic diagnostic) {
	context.addDiagnostic(std::move(diagnostic));
	return false;
}

static Range duplicateElementRange(const Range &definitionRange, const DefinitionPatternElement &element) {
	return Range(
		definitionRange.line, definitionRange.start() + element.startPos,
		definitionRange.start() + element.startPos + element.text.length()
	);
}

using SeenVariableLikes = std::unordered_map<std::string, Range>;

static std::vector<SeenVariableLikes> markDuplicateVariableLikeElementsRec(
	const Range &definitionRange, std::vector<DefinitionPatternElement> &elements, const std::vector<SeenVariableLikes> &states
) {
	std::vector<SeenVariableLikes> currentStates = states;
	for (DefinitionPatternElement &element : elements) {
		std::vector<SeenVariableLikes> nextStates;
		for (const SeenVariableLikes &state : currentStates) {
			if (element.type == PatternElement::Type::Choice) {
				for (auto &alternative : element.alternatives) {
					std::vector<SeenVariableLikes> alternativeStates =
						markDuplicateVariableLikeElementsRec(definitionRange, alternative, {state});
					nextStates.insert(nextStates.end(), alternativeStates.begin(), alternativeStates.end());
				}
				continue;
			}

			SeenVariableLikes nextState = state;
			if (element.type == PatternElement::Type::VariableLike) {
				auto it = nextState.find(element.text);
				if (it != nextState.end()) {
					if (!element.firstDuplicateVariableLikeRange.line)
						element.firstDuplicateVariableLikeRange = it->second;
				} else {
					nextState.emplace(element.text, duplicateElementRange(definitionRange, element));
				}
			}
			nextStates.push_back(std::move(nextState));
		}
		currentStates = std::move(nextStates);
	}
	return currentStates;
}

void markDuplicateVariableLikeElements(const Range &definitionRange, std::vector<DefinitionPatternElement> &elements) {
	markDuplicateVariableLikeElementsRec(definitionRange, elements, {SeenVariableLikes{}});
}

namespace {
static bool isPatternSeparator(const DefinitionPatternElement &element) {
	return element.type == PatternElement::Type::Other && !element.text.empty() &&
		   element.text.find_first_not_of(' ') == std::string::npos;
}

static std::vector<std::vector<DefinitionPatternElement>>
expandedPatternPaths(const std::vector<DefinitionPatternElement> &elements) {
	std::vector<std::vector<DefinitionPatternElement>> paths(1);
	for (const DefinitionPatternElement &element : elements) {
		if (element.type != PatternElement::Type::Choice) {
			for (auto &path : paths)
				path.push_back(element);
			continue;
		}

		std::vector<std::vector<DefinitionPatternElement>> branchedPaths;
		for (const auto &path : paths) {
			for (const auto &alternative : element.alternatives) {
				for (auto &alternativePath : expandedPatternPaths(alternative)) {
					std::vector<DefinitionPatternElement> branch = path;
					branch.insert(branch.end(), alternativePath.begin(), alternativePath.end());
					branchedPaths.push_back(std::move(branch));
				}
			}
		}
		paths = std::move(branchedPaths);
	}
	return paths;
}

static std::vector<DefinitionPatternElement> normalizePatternSeparators(const std::vector<DefinitionPatternElement> &elements) {
	std::vector<DefinitionPatternElement> normalized;
	bool pendingSeparator = false;
	size_t separatorStartPos = 0;
	for (const DefinitionPatternElement &element : elements) {
		if (isPatternSeparator(element)) {
			if (!normalized.empty() && !pendingSeparator) {
				pendingSeparator = true;
				separatorStartPos = element.startPos;
			}
			continue;
		}
		if (pendingSeparator) {
			normalized.emplace_back(PatternElement::Type::Other, " ", separatorStartPos);
			pendingSeparator = false;
		}
		normalized.push_back(element);
	}
	return normalized;
}
} // namespace

std::vector<std::vector<DefinitionPatternElement>>
canonicalPatternPaths(const std::vector<DefinitionPatternElement> &elements) {
	auto paths = expandedPatternPaths(elements);
	for (auto &path : paths)
		path = normalizePatternSeparators(path);
	return paths;
}

bool hasSingleWordPatternSpelling(const std::vector<DefinitionPatternElement> &elements) {
	for (const auto &path : canonicalPatternPaths(elements)) {
		if (path.size() == 1 && path.front().type == PatternElement::Type::VariableLike)
			return true;
	}
	return false;
}

static void markSingleWordLiteralElements(std::vector<DefinitionPatternElement> &elements) {
	std::unordered_set<size_t> literalStartPositions;
	for (const auto &path : canonicalPatternPaths(elements)) {
		if (path.size() == 1 && path.front().type == PatternElement::Type::VariableLike)
			literalStartPositions.insert(path.front().startPos);
	}

	forEachLeafElement(elements, [&](DefinitionPatternElement &element) {
		if (element.type == PatternElement::Type::VariableLike && literalStartPositions.contains(element.startPos))
			element.isLiteralInSingleWordSpelling = true;
	});
}

bool visitPatternNameWithFoundState(
	std::vector<DefinitionPatternElement> &elements, const std::string &name, bool foundBefore,
	const std::function<bool(DefinitionPatternElement &)> &onFirstMatch
) {
	bool found = foundBefore;
	for (DefinitionPatternElement &element : elements) {
		if (element.type == PatternElement::Type::Choice) {
			bool foundAfterChoice = found;
			for (auto &alternative : element.alternatives)
				foundAfterChoice = visitPatternNameWithFoundState(alternative, name, found, onFirstMatch) || foundAfterChoice;
			found = foundAfterChoice;
			continue;
		}
		if (!found && element.text == name && onFirstMatch(element))
			found = true;
	}
	return found;
}

bool parsePatternElements(
	ParseContext &context, Range patternRange, std::string_view patternString, std::vector<DefinitionPatternElement> &result,
	size_t offset
) {
	result.clear();
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
			size_t diagnosticEnd = std::min(bracketStart + offset + 1, patternRange.subString.size());
			return emitPatternParseFailure(
				context, Diagnostic(
							 context, Diagnostic::Level::Error, "invalid pattern parse unmatched bracket",
							 patternRange.subRange(static_cast<int>(bracketStart + offset), static_cast<int>(diagnosticEnd)),
							 "character", std::string(1, openBracket)
						 )
			);
		}

		// extract content between brackets
		std::string_view content = patternString.substr(bracketStart + 1, i - bracketStart - 2);

		if (isCurly) {
			// {type:name} — capture element
			size_t colonPos = content.find(':');
			if (colonPos == std::string_view::npos) {
				size_t diagnosticEnd = std::min(bracketStart + offset + 1, patternRange.subString.size());
				return emitPatternParseFailure(
					context, Diagnostic(
								 context, Diagnostic::Level::Error, "invalid pattern parse capture format",
								 patternRange.subRange(static_cast<int>(bracketStart + offset), static_cast<int>(diagnosticEnd))
							 )
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
				std::vector<DefinitionPatternElement> alternative;
				if (!parsePatternElements(context, patternRange, part, alternative, altOffset))
					return false;
				choice.alternatives.push_back(std::move(alternative));
				altOffset += part.size() + 1; // +1 for '|'
			}
			// Separator spaces around choices are normalized when patterns are
			// added to the pattern tree, so alternatives keep their authored
			// elements here.
			result.push_back(std::move(choice));
		}

		pos = i; // continue after closing bracket
	}

	if (countPatternPaths(result) > maxPatternChoiceCombinations) {
		return emitPatternParseFailure(
			context, Diagnostic(
						 context, Diagnostic::Level::Error, "pattern choice combination limit", patternRange, "limit",
						 std::to_string(maxPatternChoiceCombinations)
					 )
		);
	}

	normalizePatternElements(result);
	markDuplicateVariableLikeElements(patternRange, result);
	if (offset == 0)
		markSingleWordLiteralElements(result);
	return true;
}
