#include "precedenceSection.h"
#include "parseContext.h"

bool PrecedenceSection::addItem(ParseContext &context, Range itemRange) {
	std::string itemStr(itemRange.subString);
	if (type == SectionType::Before) {
		parent->beforePatterns.push_back(itemStr);
	} else {
		parent->afterPatterns.push_back(itemStr);
	}
	if (itemStr == "default") {
		context.addSourceToken(itemRange, ParseContext::SourceTokenKind::Keyword);
	} else {
		context.addSourceToken(itemRange, ParseContext::SourceTokenKind::PatternReference, SectionType::Expression);
	}
	return true;
}

void PrecedenceSection::addSeparator(ParseContext &context, Range separatorRange) {
	context.addSourceToken(separatorRange, ParseContext::SourceTokenKind::PatternReference, SectionType::Expression);
}
