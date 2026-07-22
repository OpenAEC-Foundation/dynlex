#include "paddingSection.h"
#include "membersSection.h"

bool PaddingSection::applyValue(ParseContext &context, CodeLine *line, int value) {
	if (!validateByteAlignment(context, line, value))
		return false;
	auto *membersSection = static_cast<MembersSection *>(parent);
	return membersSection->setNextFieldAlignment(context, line, static_cast<unsigned>(value));
}
