#include "sectionType.h"

SectionType sectionTypeFromString(std::string_view str) {
	if (str == "section")
		return SectionType::Section;
	if (str == "function")
		return SectionType::Function;
	if (str == "class")
		return SectionType::Class;
	if (str == "patterns")
		return SectionType::Pattern;
	if (str == "execute")
		return SectionType::Execute;
	if (str == "replacement")
		return SectionType::Replacement;
	if (str == "members")
		return SectionType::Members;
	if (str == "alignment")
		return SectionType::Alignment;
	if (str == "padding")
		return SectionType::Padding;
	if (str == "retain")
		return SectionType::Retain;
	if (str == "release")
		return SectionType::Release;
	if (str == "globals")
		return SectionType::Globals;
	if (str == "before")
		return SectionType::Before;
	if (str == "after")
		return SectionType::After;
	return SectionType::Custom;
}

std::string sectionTypeToString(SectionType type) {
	switch (type) {
	case SectionType::Section:
		return "section";
	case SectionType::Function:
		return "function";
	case SectionType::Class:
		return "class";
	case SectionType::Pattern:
		return "patterns";
	case SectionType::Execute:
		return "execute";
	case SectionType::Replacement:
		return "replacement";
	case SectionType::Members:
		return "members";
	case SectionType::Alignment:
		return "alignment";
	case SectionType::Padding:
		return "padding";
	case SectionType::Retain:
		return "retain";
	case SectionType::Release:
		return "release";
	case SectionType::Globals:
		return "globals";
	case SectionType::Before:
		return "before";
	case SectionType::After:
		return "after";
	default:
		return "custom";
	}
}
