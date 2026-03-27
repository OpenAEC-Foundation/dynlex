#include "stringFunctions.h"

std::vector<std::string> splitString(std::string stringToSplit, char delimiter) {
	std::vector<std::string> subStrings;

	for (size_t start = 0;; start++) {
		size_t end = stringToSplit.find(delimiter, start);
		std::string subString = stringToSplit.substr(start, end - start);
		subStrings.push_back(subString);
		if (end == std::string::npos) {
			return subStrings;
		}
		start = end + 1;
	}
}

std::vector<std::string> splitString(std::string stringToSplit, std::regex delimiter) {
	std::sregex_token_iterator iter(stringToSplit.begin(), stringToSplit.end(), delimiter, -1);
	std::sregex_token_iterator end;
	std::vector<std::string> subStrings;
	for (; iter != end; ++iter)
		subStrings.push_back(*iter);
	return subStrings;
}
