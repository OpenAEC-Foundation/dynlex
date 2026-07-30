#include "membersSection.h"
#include "classSection.h"
#include "paddingSection.h"
#include "parseContext.h"
#include "pattern/patternReference.h"
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

		Range typeRange(fieldRange.line, typeStr);
		requireCompilerInvariant(
			fieldRange.line && fieldRange.line->section && fieldRange.line->section->type == SectionType::Members,
			"class member type expression has no members section"
		);
		Section *referenceOwner = fieldRange.line->section;
		Expression *typeExpr = referenceOwner->detectPatterns(context, typeRange, SectionType::Function);
		if (!typeExpr)
			return false;
		requireCompilerInvariant(typeExpr->patternReference, "class member type expression has no root pattern reference");
		typeExpr->patternReference->purpose = PatternReference::Purpose::TypeConstraint;

		DataType type;
		type.kind = DataType::Kind::Unresolved;
		type.typeExpression = typeExpr;
		fieldDefinition = {std::string(name), Range(fieldRange.line, fieldRange.line->patternText), type};
	} else {
		context.addSourceToken(fieldRange, ParseContext::SourceTokenKind::Variable);
		fieldDefinition = {std::string(fieldText), Range(fieldRange.line, fieldRange.line->patternText), {DataType::Kind::Any}};
	}
	fieldDefinition.alignment = static_cast<MembersSection *>(fieldRange.line->section)->takeNextFieldAlignment();
	section->classDefinition->fields.push_back(fieldDefinition);
	return true;
}

bool MembersSection::processLine(ParseContext &context, CodeLine *line) { return ListingSection::processLine(context, line); }

Section *MembersSection::createSection(ParseContext &context, CodeLine *line) {
	const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
	if (matchesConfiguredKeyword(line->patternText, syntax.paddingName)) {
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

bool MembersSection::setNextFieldAlignment(ParseContext &context, CodeLine *line, unsigned alignment) {
	if (nextFieldAlignment != 0) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "padding must be followed by a member before another padding directive",
			Range(line, line->patternText)
		));
		return false;
	}
	nextFieldAlignment = alignment;
	nextFieldAlignmentRange = Range(line, line->patternText);
	return true;
}

unsigned MembersSection::takeNextFieldAlignment() {
	unsigned alignment = nextFieldAlignment;
	nextFieldAlignment = 0;
	nextFieldAlignmentRange = {};
	return alignment;
}

bool MembersSection::finalize(ParseContext &context) {
	if (nextFieldAlignment == 0)
		return true;
	context.addDiagnostic(
		Diagnostic(context, Diagnostic::Level::Error, "padding must be followed by a member", nextFieldAlignmentRange)
	);
	return false;
}
