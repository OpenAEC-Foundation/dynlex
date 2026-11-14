#pragma once
#include "parseUtils.h"
#include <cctype>
#include <string_view>
#include <utility>
#include <vector>

inline std::string_view trimSourceIndent(std::string_view text) {
	size_t first = text.find_first_not_of(" \t");
	return first == std::string_view::npos ? std::string_view{} : text.substr(first);
}

inline bool sourceWordCharacter(char character) {
	return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
}

inline std::pair<size_t, size_t> trimmedSourceSpan(std::string_view text, size_t start, size_t end) {
	while (start < end && std::isspace(static_cast<unsigned char>(text[start])))
		start++;
	while (start < end && std::isspace(static_cast<unsigned char>(text[end - 1])))
		end--;
	return {start, end};
}

inline size_t sourceStringEnd(std::string_view text, size_t quote, size_t end) {
	for (size_t index = quote + 1; index < end; index++) {
		if (text[index] == '"' && !isEscapedCharacter(text, index))
			return index;
	}
	return std::string_view::npos;
}

inline std::vector<std::pair<size_t, size_t>> sourceArgumentSpans(std::string_view text, size_t start, size_t end) {
	std::vector<std::pair<size_t, size_t>> result;
	int depth = 0;
	size_t argumentStart = start;
	for (size_t index = start; index < end; index++) {
		char character = text[index];
		if (character == '"') {
			size_t quoteEnd = sourceStringEnd(text, index, end);
			if (quoteEnd == std::string_view::npos)
				break;
			index = quoteEnd;
		} else if (character == '(' || character == '[' || character == '{') {
			depth++;
		} else if (character == ')' || character == ']' || character == '}') {
			depth--;
		} else if (character == ',' && depth == 0) {
			result.push_back(trimmedSourceSpan(text, argumentStart, index));
			argumentStart = index + 1;
		}
	}
	result.push_back(trimmedSourceSpan(text, argumentStart, end));
	return result;
}
