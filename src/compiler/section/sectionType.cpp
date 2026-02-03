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
	if (str == "patterns")
		return SectionType::Pattern;
	if (str == "execute")
		return SectionType::Execute;
	if (str == "get")
		return SectionType::Get;
	if (str == "set")
		return SectionType::Set;
	if (str == "replacement")
		return SectionType::Replacement;
	if (str == "members")
		return SectionType::Members;
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
	case SectionType::Pattern:
		return "patterns";
	case SectionType::Execute:
		return "execute";
	case SectionType::Get:
		return "get";
	case SectionType::Set:
		return "set";
	case SectionType::Replacement:
		return "replacement";
	case SectionType::Members:
		return "members";
	default:
		return "custom";
	}
}