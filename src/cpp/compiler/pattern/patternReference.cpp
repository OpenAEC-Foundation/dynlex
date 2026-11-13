#include "patternReference.h"
#include "codeLine.h"
#include "dataSections.h"
#include "section.h"

PatternReference::PatternReference(Expression *expression, SectionType patternType)
	: PatternReference(SourceExpressionSite(expression), patternType) {}

PatternReference::PatternReference(SourceExpressionSite expression, SectionType patternType)
	: sourceRange(expression.range()), pattern(std::string(sourceRange.subString)), patternType(patternType),
	  expression(expression) {}

PatternReference::~PatternReference() { delete match; }

std::optional<std::string> numericPatternArgumentSpelling(const PatternReference *reference, size_t argumentIndex) {
	if (!reference || !reference->expression || argumentIndex >= reference->expression.argumentCount())
		return std::nullopt;
	return reference->expression.argument(argumentIndex).numericSpelling();
}

bool PatternReference::indexDataDeclarationNames(const PatternMatch &matched) {
	bool changed = false;
	for (PatternDefinition *definition : matched.matchingDefinitions) {
		if (!definition->section->dataSection || definition->section->dataSection->declarationParameter.empty())
			continue;
		if (const VariableMatch *variable = dataSectionDeclaredVariable(matched, *definition)) {
			changed |= range().section()->dataVariableDeclarations.insert(variable->name).second;
			range().section()->registerExplicitVariableName(
				variable->name,
				Range(range().line, range().start() + variable->lineStartPos, range().start() + variable->lineEndPos)
			);
		}
	}
	return changed;
}

void PatternReference::resolve(PatternMatch *matchResult) {
	match = matchResult;
	if (match)
		indexDataDeclarationNames(*match);
	resolved = true;
	range().section()->decrementUnresolved();
}
