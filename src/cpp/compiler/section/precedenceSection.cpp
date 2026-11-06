#include "precedenceSection.h"
#include "parseContext.h"
#include "syntaxConfig.h"

bool PrecedenceSection::addItem(ParseContext &context, Range itemRange) {
	std::string itemStr(itemRange.subString);
	if (type == SectionType::Associativity) {
		const auto &syntax = syntaxConfigForRange(context, itemRange);
		PatternAssociativity direction = PatternAssociativity::Unspecified;
		if (itemStr == syntax.leftAssociativityName)
			direction = PatternAssociativity::Left;
		else if (itemStr == syntax.rightAssociativityName)
			direction = PatternAssociativity::Right;
		if (direction == PatternAssociativity::Unspecified || parent->associativity != PatternAssociativity::Unspecified) {
			context.addDiagnostic(Diagnostic(context, Diagnostic::Level::Error, "invalid associativity", itemRange));
			return false;
		}
		parent->associativity = direction;
		context.addSourceToken(itemRange, ParseContext::SourceTokenKind::Keyword);
		return true;
	}
	if (type == SectionType::Before) {
		parent->beforePatterns.push_back(itemStr);
	} else {
		parent->afterPatterns.push_back(itemStr);
	}
	if (itemStr == "default") {
		context.addSourceToken(itemRange, ParseContext::SourceTokenKind::Keyword);
	} else {
		context.addSourceToken(itemRange, ParseContext::SourceTokenKind::PatternReference, SectionType::Function);
	}
	return true;
}

bool PrecedenceSection::finalize(ParseContext &context) {
	if (type == SectionType::Associativity && parent->associativity == PatternAssociativity::Unspecified) {
		context.addDiagnostic(
			Diagnostic(context, Diagnostic::Level::Error, "invalid associativity", Range(openingLine, openingLine->patternText))
		);
		return false;
	}
	return true;
}

void PrecedenceSection::addSeparator(ParseContext &context, Range separatorRange) {
	context.addSourceToken(separatorRange, ParseContext::SourceTokenKind::PatternReference, SectionType::Function);
}
