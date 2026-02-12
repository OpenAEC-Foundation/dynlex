#include "membersSection.h"
#include "classSection.h"
#include "parseContext.h"

// Parse a type string, supporting primitive types and class names
static Type parseFieldType(ParseContext &context, std::string_view typeStr) {
	std::string s(typeStr);
	// Try primitive types first
	if (s == "void" || s == "bool" || s == "i8" || s == "i16" || s == "i32" || s == "i64" || s == "f32" || s == "f64" ||
		s == "pointer" || s == "string")
		return Type::fromString(s);

	// Look up class name in pattern trees
	// Class patterns are stored in the Expression tree (SectionType::Expression)
	PatternTreeNode *exprTree = context.patternTrees[(size_t)SectionType::Expression];
	if (exprTree) {
		// Walk the pattern tree matching word by word via literalChildren
		PatternTreeNode *node = exprTree;
		std::string_view remaining = typeStr;
		while (!remaining.empty() && node) {
			size_t space = remaining.find(' ');
			std::string_view word = (space != std::string_view::npos) ? remaining.substr(0, space) : remaining;
			auto it = node->literalChildren.find(std::string(word));
			if (it != node->literalChildren.end()) {
				node = it->second;
				remaining = (space != std::string_view::npos) ? remaining.substr(space + 1) : std::string_view{};
			} else {
				node = nullptr;
			}
		}
		if (node && node->matchingDefinition && node->matchingDefinition->section &&
			node->matchingDefinition->section->type == SectionType::Class) {
			auto *classSec = static_cast<ClassSection *>(node->matchingDefinition->section);
			return {Type::Kind::Class, 0, 0, classSec->classDefinition, 0};
		}
	}

	return {}; // Undeduced - unknown type
}

// Parse a single field declaration, handling optional "name as type" syntax
FieldDefinition parseFieldDeclaration(ParseContext &context, std::string_view fieldText, CodeLine *line) {
	// Look for " as " separator
	size_t asPos = fieldText.find(" as ");
	if (asPos != std::string_view::npos) {
		std::string_view name = fieldText.substr(0, asPos);
		std::string_view typeStr = fieldText.substr(asPos + 4);
		// Trim whitespace
		size_t nameEnd = name.find_last_not_of(" \t");
		if (nameEnd != std::string_view::npos)
			name = name.substr(0, nameEnd + 1);
		size_t typeStart = typeStr.find_first_not_of(" \t");
		if (typeStart != std::string_view::npos)
			typeStr = typeStr.substr(typeStart);
		size_t typeEnd = typeStr.find_last_not_of(" \t");
		if (typeEnd != std::string_view::npos)
			typeStr = typeStr.substr(0, typeEnd + 1);

		Type declaredType = parseFieldType(context, typeStr);
		return {std::string(name), Range(line, line->patternText), declaredType};
	}
	return {std::string(fieldText), Range(line, line->patternText), {}};
}

// Get natural size and alignment for a type (x86-64 ABI)
static std::pair<int, int> typeSizeAlign(const Type &t) {
	if (t.isPointer())
		return {8, 8};
	switch (t.kind) {
	case Type::Kind::Integer:
		return {t.byteSize, t.byteSize};
	case Type::Kind::Float:
		return {t.byteSize, t.byteSize};
	case Type::Kind::Bool:
		return {1, 1};
	default:
		return {8, 8}; // pointer-sized default
	}
}

// Compute the current byte offset of a non-packed struct given its fields so far
static int computeCurrentOffset(const std::vector<FieldDefinition> &fields) {
	int offset = 0;
	for (const auto &field : fields) {
		auto [size, align] = typeSizeAlign(field.declaredType);
		offset = ((offset + align - 1) / align) * align; // align up
		offset += size;
	}
	return offset;
}

// Insert padding fields to align the next field to the given byte boundary
static void insertAlignmentPadding(ClassDefinition *classDef, int alignment, CodeLine *line) {
	int offset = computeCurrentOffset(classDef->fields);
	int padding = (alignment - (offset % alignment)) % alignment;
	int padIdx = 0;
	while (padding >= 8) {
		classDef->fields.push_back({"_pad" + std::to_string(padIdx++), Range(line, line->patternText), Type::fromString("i64")}
		);
		padding -= 8;
	}
	if (padding >= 4) {
		classDef->fields.push_back({"_pad" + std::to_string(padIdx++), Range(line, line->patternText), Type::fromString("i32")}
		);
		padding -= 4;
	}
	if (padding >= 2) {
		classDef->fields.push_back({"_pad" + std::to_string(padIdx++), Range(line, line->patternText), Type::fromString("i16")}
		);
		padding -= 2;
	}
	if (padding >= 1) {
		classDef->fields.push_back({"_pad" + std::to_string(padIdx++), Range(line, line->patternText), Type::fromString("i8")});
	}
}

bool MembersSection::processLine(ParseContext &context, CodeLine *line) {
	auto *cls = static_cast<ClassSection *>(parent);

	// Handle alignment directive: "alignment: N"
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
		insertAlignmentPadding(cls->classDefinition, alignment, line);
		line->resolved = true;
		return true;
	}

	cls->classDefinition->fields.push_back(parseFieldDeclaration(context, line->patternText, line));
	line->resolved = true;
	return true;
}

Section *MembersSection::createSection(ParseContext &context, CodeLine *line) {
	context.diagnostics.push_back(
		Diagnostic(Diagnostic::Level::Error, "you can't create sections in a members section", Range(line, line->patternText))
	);
	return nullptr;
}
