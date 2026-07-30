#pragma once
#include <string>
#include <string_view>
enum class SectionType {
	// reference of a custom section
	Custom,
	Section,
	Function,
	// compiler-only pattern tree for unary conversion definitions
	Conversion,
	// a section defining a class.
	Class,
	// a section with patterns, always a child section of the main sections.
	Pattern,
	// execute of a section
	Execute,
	// get of an expression
	Get,
	// set of an expression
	Set,
	// replacement of a flex
	Replacement,
	// members of a class
	Members,
	// class alignment override
	Alignment,
	// alignment directive inside members
	Padding,
	// class-defined managed-value lifecycle bodies
	Retain,
	Release,
	// globals declaration in a function
	Globals,
	// precedence declarations
	Before,
	After,
	Count
};

SectionType sectionTypeFromString(std::string_view str);
std::string sectionTypeToString(SectionType type);
