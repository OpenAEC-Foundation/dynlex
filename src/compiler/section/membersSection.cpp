#include "membersSection.h"
#include "classSection.h"
#include "function.h"
#include "parseContext.h"
#include "patternReference.h"

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
		Function *typeExpr = new Function();
		typeExpr->range = typeRange;
		typeExpr->kind = Function::Kind::Pending;
		PatternReference *ref = new PatternReference(typeExpr, SectionType::Function);
		typeExpr->patternReference = ref;
		section->addPatternReference(ref);

		DataType type;
		type.kind = DataType::Kind::Unresolved;
		type.typeFunction = typeExpr;
		fieldDefinition = {std::string(name), Range(fieldRange.line, fieldRange.line->patternText), type};
	} else {
		context.addSourceToken(fieldRange, ParseContext::SourceTokenKind::Variable);
		fieldDefinition = {std::string(fieldText), Range(fieldRange.line, fieldRange.line->patternText), {DataType::Kind::Any}};
	}
	section->classDefinition->fields.push_back(fieldDefinition);
	return true;
}

bool MembersSection::processLine(ParseContext &context, CodeLine *line) {
	auto *cls = static_cast<ClassSection *>(parent);

	// Handle alignment directive: "padding: N"
	std::string_view text = line->patternText;
	if (text.starts_with("padding:")) {
		size_t colonPos = text.find(':');
		std::string_view numStr = text.substr(colonPos + 1);
		size_t start = numStr.find_first_not_of(" \t");
		if (start != std::string_view::npos)
			numStr = numStr.substr(start);
		int alignment = std::stoi(std::string(numStr));
		context.addSourceToken(Range(line, text.substr(0, colonPos + 1)), ParseContext::SourceTokenKind::Keyword);
		if (!numStr.empty())
			context.addSourceToken(Range(line, numStr), ParseContext::SourceTokenKind::Number);
		if (alignment > cls->classDefinition->alignment)
			cls->classDefinition->alignment = alignment;
		line->resolved = true;
		return true;
	}

	// Delegate to base class for comma-separated parsing
	return ListingSection::processLine(context, line);
}

bool MembersSection::addItem(ParseContext &context, Range itemRange) {
	auto *cls = static_cast<ClassSection *>(parent);
	return parseFieldDeclaration(context, itemRange, cls);
}

void MembersSection::addSeparator(ParseContext &context, Range separatorRange) {
	context.addSourceToken(separatorRange, ParseContext::SourceTokenKind::Keyword);
}
