#include "patternDefinitionTreeType.inl"

static bool prepareConversionPattern(ParseContext &context, PatternDefinition &definition) {
	Section *section = definition.section;
	if (!section || !section->isConversion)
		return true;
	if (definition.patternElements.size() == 1) {
		DefinitionPatternElement &element = definition.patternElements.front();
		if (element.type == PatternElement::Type::VariableLike && !element.isExplicitLiteral) {
			element.type = PatternElement::Type::Variable;
			return true;
		}
		if (element.type == PatternElement::Type::Variable)
			return true;
	}
	context.diagnostics.push_back(
		Diagnostic(context, Diagnostic::Level::Error, "conversion requires one parameter", definition.range)
	);
	return false;
}
