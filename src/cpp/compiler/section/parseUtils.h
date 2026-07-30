#pragma once
#include <functional>
#include <string_view>

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
