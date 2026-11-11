#pragma once
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

inline constexpr std::array<std::pair<char, char>, 10> sourceStringEscapes = {
	{{'n', '\n'},
	 {'t', '\t'},
	 {'r', '\r'},
	 {'a', '\a'},
	 {'b', '\b'},
	 {'f', '\f'},
	 {'v', '\v'},
	 {'\\', '\\'},
	 {'"', '"'},
	 {'0', '\0'}}
};

// Parsing and source-value presentation share exactly the same escape alphabet.
inline std::string processEscapeSequences(std::string_view input) {
	std::string result;
	result.reserve(input.size());
	for (size_t index = 0; index < input.size(); ++index) {
		if (input[index] == '\\' && index + 1 < input.size()) {
			char spelling = input[++index];
			auto escape = std::find_if(sourceStringEscapes.begin(), sourceStringEscapes.end(), [spelling](auto entry) {
				return entry.first == spelling;
			});
			result += escape != sourceStringEscapes.end() ? escape->second : spelling;
		} else {
			result += input[index];
		}
	}
	return result;
}

inline std::string quotedSourceString(std::string_view input) {
	std::string result;
	result.reserve(input.size() + 2);
	result += '"';
	for (char character : input) {
		auto escape = std::find_if(sourceStringEscapes.begin(), sourceStringEscapes.end(), [character](auto entry) {
			return entry.second == character;
		});
		if (escape != sourceStringEscapes.end()) {
			result += '\\';
			result += escape->first;
		} else {
			result += character;
		}
	}
	result += '"';
	return result;
}
