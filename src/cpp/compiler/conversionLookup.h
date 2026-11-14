#pragma once
#include "compiler.h"
#include "parseContext.h"
#include "patternTreeNode.h"
#include "section.h"

// The same visible registry supplies selection and incremental lookup inputs.
inline std::vector<PatternDefinition *>
visibleUserConversions(ParseContext &context, const lsp::SourceFile &source, bool implicitOnly) {
	std::vector<PatternDefinition *> result;
	PatternTreeNode *root = context.patternTrees[static_cast<int>(SectionType::Conversion)];
	if (!root || !root->argumentChild)
		return result;
	for (PatternDefinition *definition : root->argumentChild->matchingDefinitions)
		if (definition->section->isConversion && (!implicitOnly || definition->section->isImplicitConversion) &&
			isPatternDefinitionVisibleFromSource(*definition, source))
			result.push_back(definition);
	return result;
}
