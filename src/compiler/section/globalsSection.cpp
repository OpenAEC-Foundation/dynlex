#include "globalsSection.h"
#include "parseContext.h"

bool GlobalsSection::addItem(ParseContext &context, Range itemRange) {
	// Add to parent Function/Effect section's globalVariables list
	std::string varNameStr(itemRange.subString);
	parent->globalVariables.push_back(varNameStr);
	context.declaredGlobalVariables.insert(varNameStr);
	context.addSourceToken(itemRange, ParseContext::SourceTokenKind::Variable);
	return true;
}

void GlobalsSection::addSeparator(ParseContext &context, Range separatorRange) {
	context.addSourceToken(separatorRange, ParseContext::SourceTokenKind::Keyword);
}
