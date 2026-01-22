#include "sectionType.h"

SectionType sectionTypeFromString(std::string_view str) {
	if (str == "section")
		return SectionType::Section;
	if (str == "expression")
		return SectionType::Expression;
	if (str == "effect")
		return SectionType::Effect;
	if (str == "class")
		return SectionType::Class;
	return SectionType::Custom;
}

std::string sectionTypeToString(SectionType type) {
	switch (type) {
	case SectionType::Section:
		return "section";
	case SectionType::Expression:
		return "expression";
	case SectionType::Effect:
		return "effect";
	case SectionType::Class:
		return "class";
	default:
		return "custom";
	}
}