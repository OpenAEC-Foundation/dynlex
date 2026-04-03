#include "membersSection.h"
#include "classSection.h"
#include "expression.h"
#include "paddingSection.h"
#include "parseContext.h"
#include "patternReference.h"
#include "syntaxConfig.h"

// Parse a single field declaration, handling optional "name as type" syntax
bool parseFieldDeclaration(ParseContext &context, Range fieldRange, ClassSection *section) {
	// this is hardcoded syntax and will have to be replaced later.
	std::string_view fieldText = fieldRange.subString;
	std::string_view separator = " as ";
	// Look for " as " separator
	size_t asPos = fieldText.find(separator);
	FieldDefinition fieldDefinition;
	if (asPos != std::string_view::npos) {
		std::string_view name = fieldText.substr(0, asPos);
		std::string_view typeStr = fieldText.substr(asPos + separator.length());
		context.addSourceToken(fieldRange.subRange(0, static_cast<int>(asPos)), ParseContext::SourceTokenKind::Variable);
		context.addSourceToken(
			fieldRange.subRange(static_cast<int>(asPos + 1), static_cast<int>(asPos + 3)),
			ParseContext::SourceTokenKind::Keyword
		);

		// Create a class pattern reference for the type text
		Range typeRange(fieldRange.line, typeStr);
		Expression *typeExpr = new Expression();
		typeExpr->range = typeRange;
		typeExpr->kind = Expression::Kind::Pending;
		PatternReference *ref = new PatternReference(typeExpr, SectionType::Function);
		typeExpr->patternReference = ref;
		Section *referenceOwner = fieldRange.line && fieldRange.line->section ? fieldRange.line->section : section;
		referenceOwner->addPatternReference(ref);

		DataType type;
		type.kind = DataType::Kind::Unresolved;
		type.typeExpression = typeExpr;
		fieldDefinition = {std::string(name), Range(fieldRange.line, fieldRange.line->patternText), type};
	} else {
		context.addSourceToken(fieldRange, ParseContext::SourceTokenKind::Variable);
		fieldDefinition = {std::string(fieldText), Range(fieldRange.line, fieldRange.line->patternText), {DataType::Kind::Any}};
	}
	section->classDefinition->fields.push_back(fieldDefinition);
	return true;
}

bool MembersSection::processLine(ParseContext &context, CodeLine *line) { return ListingSection::processLine(context, line); }

Section *MembersSection::createSection(ParseContext &context, CodeLine *line) {
	if (line->patternText == "padding") {
		return new PaddingSection(this);
	}

	return ListingSection::createSection(context, line);
}

bool MembersSection::addItem(ParseContext &context, Range itemRange) {
	auto *cls = static_cast<ClassSection *>(parent);
	return parseFieldDeclaration(context, itemRange, cls);
}

void MembersSection::addSeparator(ParseContext &context, Range separatorRange) {
	context.addSourceToken(separatorRange, ParseContext::SourceTokenKind::Keyword);
}
