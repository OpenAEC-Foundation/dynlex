#include "codeLine.h"
#include "section.h"

bool CodeLine::isPatternDefinition() const { return sectionOpening && sectionOpening->type != SectionType::Custom; }

bool CodeLine::isPatternReference() const {
	return patternText.length() && (!sectionOpening || sectionOpening->type == SectionType::Custom);
}
