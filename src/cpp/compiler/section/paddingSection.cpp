#include "paddingSection.h"
#include "classSection.h"
#include "membersSection.h"

bool PaddingSection::applyValue(ParseContext & /*context*/, CodeLine * /*line*/, int value) {
	auto *membersSection = static_cast<MembersSection *>(parent);
	auto *classSection = static_cast<ClassSection *>(membersSection->parent);
	if (value > classSection->classDefinition->alignment)
		classSection->classDefinition->alignment = value;
	return true;
}
