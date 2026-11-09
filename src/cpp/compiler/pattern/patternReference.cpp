#include "patternReference.h"
#include "codeLine.h"
#include "dataSections.h"
#include "section.h"

PatternReference::PatternReference(Expression *expression, SectionType patternType)
	: sourceRange(expression->range), pattern(std::string(expression->range.subString)), patternType(patternType),
	  expression(expression) {}

PatternReference::~PatternReference() { delete match; }

std::optional<std::string> numericPatternArgumentSpelling(const PatternReference *reference, size_t argumentIndex) {
	if (!reference || !reference->expression || argumentIndex >= reference->expression->arguments.size())
		return std::nullopt;
	const Expression *argument = reference->expression->arguments[argumentIndex];
	if (!argument || argument->kind != Expression::Kind::Literal || !argument->range.line)
		return std::nullopt;
	const bool numeric = std::holds_alternative<std::int64_t>(argument->literalValue) ||
						 std::holds_alternative<MinimumSignedIntegerMagnitude>(argument->literalValue) ||
						 std::holds_alternative<double>(argument->literalValue);
	return numeric ? std::optional<std::string>(argument->range.subString) : std::nullopt;
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
