#pragma once
#include <regex>
#include <string>
#include <string_view>
#include <vector>
std::vector<std::string> splitString(std::string stringToSplit, char delimiter);
std::vector<std::string> splitString(std::string stringToSplit, std::regex delimiter);

// implement this function, but for your own type
template <typename enumType> inline bool enumToString(enumType, std::string &) { return false; }

template <typename enumType> inline bool stringToEnum(std::string_view, enumType &) { return false; }

template <typename enumType> inline std::string enumToString(enumType enumVal) {
	std::string result;
	if (enumToString(enumVal, result)) {
		return result;
	} else {
		throw "enum couldn't be converted: " + std::to_string((std::underlying_type_t<enumType>)enumVal);
	}
}

template <typename enumType> inline enumType stringToEnum(std::string_view enumString) {
	enumType result;
	if (stringToEnum(enumString, result)) {
		return result;
	} else {
		throw "string couldn't be converted: '" + std::string(enumString) + "'";
	}
}