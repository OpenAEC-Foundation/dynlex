#include "patternDefinition.h"
#include "compilerUtils.h"
#include "section.h"

PatternDefinition::PatternDefinition(Range range, Section *section) : range(range), section(section) {}

bool isPatternDefinitionVisibleFromSource(const PatternDefinition &definition, const lsp::SourceFile &sourceFile) {
	requireCompilerInvariant(definition.section != nullptr, "pattern definition visibility requires an owning section");
	requireCompilerInvariant(
		definition.range.line && definition.range.line->sourceFile, "pattern definition visibility requires a source location"
	);
	if (!definition.section->isLocal)
		return true;
	return definition.range.line->sourceFile == &sourceFile;
}

bool patternDefinitionsShareVisibilityScope(const PatternDefinition &left, const PatternDefinition &right) {
	requireCompilerInvariant(left.section != nullptr, "left pattern definition is missing its owning section");
	requireCompilerInvariant(right.section != nullptr, "right pattern definition is missing its owning section");
	requireCompilerInvariant(
		left.range.line && left.range.line->sourceFile, "left pattern definition is missing its source location"
	);
	requireCompilerInvariant(
		right.range.line && right.range.line->sourceFile, "right pattern definition is missing its source location"
	);
	if (!left.section->isLocal || !right.section->isLocal)
		return true;
	return left.range.line->sourceFile == right.range.line->sourceFile;
}
