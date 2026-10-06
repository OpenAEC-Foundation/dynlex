#pragma once
#include <functional>
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

// Parse a comma-separated list and call the callback for each item
inline void parseCommaSeparatedList(std::string_view text, std::function<void(std::string_view)> callback) {
	while (!text.empty()) {
		// Trim leading whitespace
		size_t start = text.find_first_not_of(" \t");
		if (start == std::string_view::npos)
			break;
		text = text.substr(start);

		// Find next comma
		size_t comma = text.find(',');
		std::string_view item = (comma != std::string_view::npos) ? text.substr(0, comma) : text;

		// Trim trailing whitespace
		size_t end = item.find_last_not_of(" \t");
		if (end != std::string_view::npos)
			item = item.substr(0, end + 1);

		if (!item.empty())
			callback(item);

		if (comma == std::string_view::npos)
			break;
		text = text.substr(comma + 1);
	}
}

template <typename Callback, typename SeparatorCallback>
inline bool parseCommaSeparatedListWithRanges(std::string_view text, Callback callback, SeparatorCallback separatorCallback) {
	size_t cursor = 0;
	while (cursor < text.size()) {
		size_t start = text.find_first_not_of(" \t", cursor);
		if (start == std::string_view::npos)
			break;

		size_t comma = text.find(',', start);
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
