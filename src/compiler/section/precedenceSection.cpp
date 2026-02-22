#include "precedenceSection.h"

bool PrecedenceSection::addItem(ParseContext & /*context*/, std::string_view item, CodeLine * /*line*/) {
	std::string itemStr(item);
	if (type == SectionType::Before) {
		parent->beforePatterns.push_back(itemStr);
	} else {
		parent->afterPatterns.push_back(itemStr);
	}
	return true;
}
