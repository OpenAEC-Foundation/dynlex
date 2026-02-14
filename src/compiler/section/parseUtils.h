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
