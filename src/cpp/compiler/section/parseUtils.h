#pragma once
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>

// A character is escaped only by an odd run of preceding backslashes.
inline bool isEscapedCharacter(std::string_view text, size_t index) {
	size_t start = index;
	while (start > 0 && text[start - 1] == '\\')
		--start;
	return (index - start) % 2 != 0;
}

// Decode the escape sequences accepted in source string literals.
inline std::string processEscapeSequences(std::string_view input) {
	static const std::unordered_map<char, char> escapes = {{'n', '\n'}, {'t', '\t'}, {'r', '\r'},  {'a', '\a'}, {'b', '\b'},
														   {'f', '\f'}, {'v', '\v'}, {'\\', '\\'}, {'"', '"'},	{'0', '\0'}};
	std::string result;
	result.reserve(input.size());
	for (size_t i = 0; i < input.size(); ++i) {
		if (input[i] == '\\' && i + 1 < input.size()) {
			auto it = escapes.find(input[++i]);
			result += (it != escapes.end()) ? it->second : input[i];
		} else {
			result += input[i];
		}
	}
	return result;
}

inline size_t nextListingSeparator(std::string_view text, size_t start) {
	std::stack<char> brackets;
	bool quoted = false;
	for (size_t index = start; index < text.size(); ++index) {
		char character = text[index];
		if (character == '"' && !isEscapedCharacter(text, index)) {
			quoted = !quoted;
			continue;
		}
		if (quoted)
			continue;
		if (character == '(' || character == '[' || character == '{') {
			brackets.push(character);
		} else if (character == ')' || character == ']' || character == '}') {
			// The item's bracket parser reports malformed source at its own range.
			if (brackets.empty() || (character == ')' && brackets.top() != '(') ||
				(character == ']' && brackets.top() != '[') || (character == '}' && brackets.top() != '{'))
				return std::string_view::npos;
			brackets.pop();
		} else if (character == ',' && brackets.empty()) {
			return index;
		}
	}
	return std::string_view::npos;
}

template <typename Callback, typename SeparatorCallback>
inline bool parseCommaSeparatedListWithRanges(std::string_view text, Callback callback, SeparatorCallback separatorCallback) {
	size_t cursor = 0;
	while (cursor < text.size()) {
		size_t start = text.find_first_not_of(" \t", cursor);
		if (start == std::string_view::npos)
			break;

		size_t comma = nextListingSeparator(text, start);
		size_t itemEnd = (comma != std::string_view::npos) ? comma : text.size();
		std::string_view item = text.substr(start, itemEnd - start);

		size_t trimmedEnd = item.find_last_not_of(" \t");
		if (trimmedEnd != std::string_view::npos) {
			item = item.substr(0, trimmedEnd + 1);
			if (!callback(item, start, start + item.size()))
				return false;
		}

		if (comma == std::string_view::npos)
			break;
		size_t separatorEnd = text.find_first_not_of(" \t", comma + 1);
		if (separatorEnd == std::string_view::npos)
			separatorEnd = text.size();
		separatorCallback(text.substr(comma, separatorEnd - comma), comma, separatorEnd);
		cursor = separatorEnd;
	}
	return true;
}

template <typename Callback> inline bool parseCommaSeparatedListWithRanges(std::string_view text, Callback callback) {
	return parseCommaSeparatedListWithRanges(text, callback, [](std::string_view, size_t, size_t) {});
}
