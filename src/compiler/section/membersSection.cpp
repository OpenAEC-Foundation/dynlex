#include "membersSection.h"
#include "classSection.h"
#include "expression.h"
#include "parseContext.h"
#include "patternReference.h"

// Parse a single field declaration, handling optional "name as type" syntax
bool parseFieldDeclaration(ParseContext & /*context*/, std::string_view fieldText, CodeLine *line, ClassSection *section) {
	// this is hardcoded syntax and will have to be replaced later.
	std::string_view separator = " as ";
	// Look for " as " separator
	size_t asPos = fieldText.find(separator);
	FieldDefinition fieldDefinition;
	if (asPos != std::string_view::npos) {
		std::string_view name = fieldText.substr(0, asPos);
		std::string_view typeStr = fieldText.substr(asPos + separator.length());

		// Create a class pattern reference for the type text
		Range typeRange(line, typeStr);
		Expression *typeExpr = new Expression();
		typeExpr->range = typeRange;
		typeExpr->kind = Expression::Kind::Pending;
		PatternReference *ref = new PatternReference(typeExpr, SectionType::Expression);
		typeExpr->patternReference = ref;
		section->addPatternReference(ref);

		DataType type;
		type.kind = DataType::Kind::Unresolved;
		type.typeExpression = typeExpr;
		fieldDefinition = {std::string(name), Range(line, line->patternText), type};
	} else {
		fieldDefinition = {std::string(fieldText), Range(line, line->patternText), {DataType::Kind::Any}};
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
		if (alignment > cls->classDefinition->alignment)
			cls->classDefinition->alignment = alignment;
		line->resolved = true;
		return true;
	}

	// Delegate to base class for comma-separated parsing
	return ListingSection::processLine(context, line);
}

bool MembersSection::addItem(ParseContext &context, std::string_view fieldText, CodeLine *line) {
	auto *cls = static_cast<ClassSection *>(parent);
	return parseFieldDeclaration(context, fieldText, line, cls);
}
