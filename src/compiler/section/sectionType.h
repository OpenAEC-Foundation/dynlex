#pragma once
#include <string>
#include <string_view>
enum class SectionType {
	Custom,
	Section,
	Expression,
	Effect,
	// a section defining a class.
	Class,
	// a section with patterns, always a child section of the main sections.
	Pattern,
	Count
};

SectionType sectionTypeFromString(std::string_view str);
std::string sectionTypeToString(SectionType type);