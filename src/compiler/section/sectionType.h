#pragma once
#include <string>
#include <string_view>
enum class SectionType {
	// reference of a custom section
	Custom,
	Section,
	Expression,
	Effect,
	// a section defining a class.
	Class,
	// a section with patterns, always a child section of the main sections.
	Pattern,
	// execute of a section or effect
	Execute,
	// get of an expression
	Get,
	// set of an expression
	Set,
	// replacement of a macro
	Replacement,
	// members of a class
	Members,
	Count
};

SectionType sectionTypeFromString(std::string_view str);
std::string sectionTypeToString(SectionType type);