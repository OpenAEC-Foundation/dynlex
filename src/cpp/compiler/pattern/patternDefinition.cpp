#include "patternDefinition.h"
#include "compilerUtils.h"
#include "parseContext.h"
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

void mutatePatternDefinition(ParseContext &context, PatternDefinition &definition, const std::function<void()> &mutation) {
	requireCompilerInvariant(static_cast<bool>(mutation), "pattern definition mutation requires an operation");
	if (!definition.indexedTree) {
		requireCompilerInvariant(
			definition.indexedTreeType == SectionType::Count && definition.indexedPaths.empty() &&
				definition.indexedNodePaths.empty() && definition.endNodes.empty(),
			"unindexed pattern definition retains trie metadata"
		);
		mutation();
		return;
	}

	requireCompilerInvariant(
		static_cast<bool>(context.indexedPatternDefinitionMutation),
		"indexed pattern definition changed outside the resolution transaction"
	);
	context.indexedPatternDefinitionMutation(definition, mutation);
}

void promoteImplicitPatternParameter(
	ParseContext &context, PatternDefinition &definition, DefinitionPatternElement &element, const Range &useRange
) {
	requireCompilerInvariant(canPromoteVariableLikeElement(element), "invalid implicit pattern parameter promotion");
	mutatePatternDefinition(context, definition, [&]() {
		element.promotedFromVariableLike = true;
		if (!element.firstImplicitPromotionUseRange.line)
			element.firstImplicitPromotionUseRange = useRange;
		element.type = PatternElement::Type::Variable;
	});
}

void revertImplicitPatternParameter(ParseContext &context, PatternDefinition &definition, DefinitionPatternElement &element) {
	requireCompilerInvariant(
		element.type == PatternElement::Type::Variable && element.typeConstraintName.empty() &&
			element.promotedFromVariableLike,
		"invalid implicit pattern parameter reversion"
	);
	mutatePatternDefinition(context, definition, [&]() {
		element.type = PatternElement::Type::VariableLike;
		element.promotedFromVariableLike = false;
		element.firstImplicitPromotionUseRange = {};
	});
}
