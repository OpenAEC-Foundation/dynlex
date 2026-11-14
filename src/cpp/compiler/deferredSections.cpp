#include "compiler.h"
#include "dataSections.h"
#include "parseContext.h"
#include "pattern/patternDefinition.h"
#include "pattern/patternReference.h"
#include "section/functionSection.h"
#include "section/parseUtils.h"
#include "section/variable.h"
#include <algorithm>

// This creates a normal callable AST from an already parsed body. It does not
// rewrite source or interpret user statements in a separate language.
bool prepareDeferredSectionBodies(ParseContext &context) {
	std::vector<CodeLine *> candidates = context.codeLines;
	size_t serial = 0;
	for (CodeLine *line : candidates) {
		if (!line->sectionOpening || !line->expression.rootReference())
			continue;
		PatternReference *reference = line->expression.rootReference();
		auto definitions = context.sourceDefinitions(reference);
		if (definitions.size() != 1)
			continue;
		Section *definition = definitions.front()->section;
		if (!definition->dataSection || !definition->dataSection->receiverType)
			continue;
		if (context.demandBodyResolution)
			context.demandBodyResolution(line->section);
		Section *source = line->sectionOpening;
		Section *parent = source->parent;
		SourceLocation anchor = Range(line, line->patternText).sourceStart();
		auto generatedLine = [&](std::string text) {
			CodeLine *generated = context.createCodeLine(
				line->sourceFile, line->sourceFileLineIndex, std::move(text),
				{{0, 0, anchor.sourceFile, anchor.sourceFileLineIndex, anchor.column}}
			);
			generated->mergedLineIndex = line->mergedLineIndex;
			generated->synthetic = true;
			return generated;
		};
		auto position = std::ranges::find(parent->children, source);
		requireCompilerInvariant(position != parent->children.end(), "deferred section has no parent slot");
		size_t slot = position - parent->children.begin();
		auto *function = new FunctionSection(parent);
		function->declarationProducer = parent;
		parent->children[slot] = function;
		parent->children.pop_back();
		function->startLineIndex = source->startLineIndex;
		function->endLineIndex = source->endLineIndex;
		function->returnContract = DefinitionReturnContract::Inferred;
		auto *execute = new Section(SectionType::Get, function);
		function->executionSection = execute;
		CodeLine *bodyHeader = generatedLine("");
		bodyHeader->section = function;
		bodyHeader->sectionOpening = execute;
		bodyHeader->resolved = true;
		function->codeLines.push_back(bodyHeader);
		execute->openingLine = bodyHeader;
		execute->startLineIndex = source->startLineIndex;
		execute->endLineIndex = source->endLineIndex;
		execute->children = std::move(source->children);
		execute->codeLines = std::move(source->codeLines);
		execute->patternReferences = std::move(source->patternReferences);
		execute->variableReferences = std::move(source->variableReferences);
		execute->variableDefinitions = std::move(source->variableDefinitions);
		execute->variables = std::move(source->variables);
		execute->explicitVariableDeclarations = std::move(source->explicitVariableDeclarations);
		execute->dataVariableDeclarations = std::move(source->dataVariableDeclarations);
		for (Section *child : execute->children)
			child->parent = execute;
		for (CodeLine *bodyLine : execute->codeLines)
			bodyLine->section = execute;
		for (PatternReference *reference : execute->patternReferences) {
			if (reference->matchingScope == source)
				reference->matchingScope = execute;
		}
		std::string pattern = "__section_body_" + std::to_string(serial++) + " {__section_receiver}";
		CodeLine *header = generatedLine(std::move(pattern));
		header->section = function;
		header->patternText = header->fullText;
		function->openingLine = header;
		auto *callable = new PatternDefinition(Range(header, header->fullText), function);
		if (!parsePatternElements(context, callable->range, callable->range.subString, callable->patternElements))
			return false;
		callable->resolved = true;
		function->patternDefinitions.push_back(callable);
		function->indexExplicitParameters(*callable);
		function->receiverParameter = function->resolvePatternParameterBinding(context, "__section_receiver", callable->range);
		requireCompilerInvariant(function->receiverParameter, "deferred body has no receiver parameter");
		context.patternTrees[static_cast<size_t>(SectionType::Function)]->addPatternDefinition(callable, SectionType::Function);
		function->patternDefinitionsResolved = true;
		line->expression->deferredFunction = function;
		line->sectionOpening = function;
		delete source;
	}
	return true;
}
