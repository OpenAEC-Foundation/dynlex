#include "alignmentSection.h"
#include "classSection.h"

bool AlignmentSection::applyValue(ParseContext & /*context*/, CodeLine * /*line*/, int value) {
	static_cast<ClassSection *>(parent)->classDefinition->alignment = value;
	return true;
}
