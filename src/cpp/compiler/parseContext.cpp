#include "parseContext.h"
#include "compileTimeValue.h"
#include "expression.h"
#include "matchProgress.h"
#include "patternDefinition.h"
#include "patternReference.h"
#include "patternTreeNode.h"
#include "section/classSection.h"
#include "section/variable.h"
#include "variableReference.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Target/TargetMachine.h"
#include <iostream>
#include <iterator>
#include <unordered_set>

namespace {
void deleteExpressionTree(Expression *expression, std::unordered_set<Expression *> &visited) {
	if (!expression || !visited.insert(expression).second)
		return;
	for (Expression *argument : expression->arguments)
		deleteExpressionTree(argument, visited);
	delete expression;
}

void deletePatternTree(PatternTreeNode *node, std::unordered_set<PatternTreeNode *> &visited) {
	if (!node || !visited.insert(node).second)
		return;
	for (auto &[_, child] : node->literalChildren)
		deletePatternTree(child, visited);
	deletePatternTree(node->argumentChild, visited);
	delete node;
}
} // namespace

ParseContext::ParseContext() = default;

void ParseContext::printDiagnostics() {
	for (Diagnostic d : diagnostics) {
		std::cerr << d.toString() << "\n";
	}
}

PatternMatch *ParseContext::match(PatternReference *reference, MatchOptions options, MatchDependencies *dependencies) {
	requireCompilerInvariant(
		!dependencies || !options.acceptLiterals,
		"failed match dependency tracking does not support literal-acceptance ordering"
	);
	MatchStorage storage;
	std::vector<MatchProgress> queue;
	queue.emplace_back(this, reference, options);
	std::unordered_map<MatchControlState, MatchParentAlternatives *, MatchControlStateHash> memoizedStates;
	auto recordDependencies = [&]() {
		if (!dependencies)
			return;
		dependencies->clear();
		for (const auto &entry : memoizedStates) {
			const MatchControlState &state = entry.first;
			collectMatchDependencies(state, *dependencies);
		}
		if (!queue.empty())
			collectMatchDependencies(queue.back().controlState(), *dependencies);
		normalizeMatchDependencies(*dependencies);
	};
	size_t steps = 0;
	while (queue.size()) {
		if (options.maxSteps > 0 && steps >= options.maxSteps) {
			recordDependencies();
			return nullptr;
		}
		MatchProgress &currentProgress = queue.back();
		auto [memoizedState, inserted] = memoizedStates.try_emplace(currentProgress.controlState(), currentProgress.parents);
		if (!inserted) {
			std::vector<MatchProgress> resumedProgresses;
			MatchParentAlternatives *canonicalParents = memoizedState->second;
			MatchParentAlternatives *incomingParents = currentProgress.parents;
			if (canonicalParents && incomingParents && canonicalParents != incomingParents) {
				std::vector<const MatchProgress *> addedParents;
				for (const MatchProgress *incomingParent : incomingParents->values) {
					if (!canonicalParents->addParent(incomingParent))
						continue;
					addedParents.push_back(incomingParent);
				}
				for (auto addedParent = addedParents.rbegin(); addedParent != addedParents.rend(); addedParent++) {
					for (auto completion = canonicalParents->completedSubmatches.rbegin();
						 completion != canonicalParents->completedSubmatches.rend(); completion++) {
						resumedProgresses.push_back(MatchProgress::resumeParent(storage, **addedParent, *completion));
					}
				}
			}
			queue.pop_back();
			queue.insert(
				queue.end(), std::make_move_iterator(resumedProgresses.begin()),
				std::make_move_iterator(resumedProgresses.end())
			);
			continue;
		}
		steps++;
		std::vector<PatternDefinition *> visibleDefinitions = currentProgress.visibleDefinitions();
		MatchStep matchStep = currentProgress.step(storage, visibleDefinitions);
		if (currentProgress.isSubmatchComplete(visibleDefinitions)) {
			requireCompilerInvariant(matchStep.hasCompletedSubmatch, "completed matcher state did not produce submatch data");
			currentProgress.parents->addCompletion(std::move(matchStep.completedSubmatch));
		}
		if (currentProgress.isComplete(visibleDefinitions))
			return new PatternMatch(currentProgress.materializeMatch(storage, visibleDefinitions));
		queue.pop_back();
		queue.insert(
			queue.end(), std::make_move_iterator(matchStep.nextMatches.begin()),
			std::make_move_iterator(matchStep.nextMatches.end())
		);
	}
	recordDependencies();
	return nullptr;
}

void ParseContext::registerShaderUniform(std::string uniformName, std::uint32_t binding, Range range) {
	shaderUniforms.push_back({std::move(uniformName), binding, std::move(range)});
}

void ParseContext::registerShaderInterpolantName(const std::string &interpolantName) {
	if (interpolantName.empty())
		return;
	if (std::find(shaderInterpolantNames.begin(), shaderInterpolantNames.end(), interpolantName) ==
		shaderInterpolantNames.end())
		shaderInterpolantNames.push_back(interpolantName);
}

VariableReference *ParseContext::createVariableReference(Range range, const std::string &name) {
	ownedVariableReferences.push_back(std::make_unique<VariableReference>(std::move(range), name));
	return ownedVariableReferences.back().get();
}

namespace {
Expression *cloneExpressionTreeImpl(ParseContext &context, Expression *expression, bool preserveInferenceMetadata) {
	if (!expression)
		return nullptr;
	Expression *clone = new Expression();
	context.ownedClonedExpressions.push_back(clone);
	clone->kind = expression->kind;
	clone->range = expression->range;
	clone->literalValue = expression->literalValue;
	clone->variable = expression->variable;
	clone->patternMatch = expression->patternMatch;
	clone->patternReference = expression->patternReference;
	clone->intrinsicName = expression->intrinsicName;
	clone->inferredFlexExpansion = nullptr;
	clone->inferredConversion = nullptr;
	clone->inferredPointerStorage = nullptr;
	clone->inferredFlexBody = preserveInferenceMetadata ? expression->inferredFlexBody : nullptr;
	clone->sectionOutcome = preserveInferenceMetadata ? expression->sectionOutcome : Expression::SectionOutcome{};
	clone->executionFallsThrough = preserveInferenceMetadata ? expression->executionFallsThrough : std::nullopt;
	clone->sectionBodyReachable = !preserveInferenceMetadata || expression->sectionBodyReachable;
	clone->sectionBodyInferred = preserveInferenceMetadata && expression->sectionBodyInferred;
	clone->sectionBodyFallsThrough = !preserveInferenceMetadata || expression->sectionBodyFallsThrough;
	clone->branchSelection = preserveInferenceMetadata ? expression->branchSelection : std::nullopt;
	clone->reusableTemplateExpression =
		expression->reusableTemplateExpression ? expression->reusableTemplateExpression : expression;
	clone->isSubMatch = expression->isSubMatch;
	clone->isExplicitGroup = expression->isExplicitGroup;
	clone->groupingArgumentIndices = expression->groupingArgumentIndices;
	clone->groupingArgumentHasAdjacentSiblingSlot = expression->groupingArgumentHasAdjacentSiblingSlot;
	clone->groupingStartsWithArgument = expression->groupingStartsWithArgument;
	clone->groupingEndsWithArgument = expression->groupingEndsWithArgument;
	clone->type = preserveInferenceMetadata ? expression->type : DataType{};
	clone->selectedPatternDefinition = preserveInferenceMetadata ? expression->selectedPatternDefinition : nullptr;
	clone->selectedPatternPathIndex = preserveInferenceMetadata ? expression->selectedPatternPathIndex : std::nullopt;
	clone->selectedCallableDefinition = preserveInferenceMetadata ? expression->selectedCallableDefinition : nullptr;
	clone->selectedCallablePathIndex = preserveInferenceMetadata ? expression->selectedCallablePathIndex : std::nullopt;
	clone->selectedInstantiation = preserveInferenceMetadata ? expression->selectedInstantiation : nullptr;
	clone->subjectSetter = nullptr;
	clone->compileTimeValue = preserveInferenceMetadata ? expression->compileTimeValue : CompileTimeValue{};
	clone->minimumIntegerEffects =
		preserveInferenceMetadata ? expression->minimumIntegerEffects : MinimumSignedIntegerMagnitudeEffects{};
	clone->arguments.reserve(expression->arguments.size());
	for (Expression *argument : expression->arguments)
		clone->arguments.push_back(cloneExpressionTreeImpl(context, argument, preserveInferenceMetadata));
	return clone;
}
} // namespace

Expression *ParseContext::cloneExpressionTree(Expression *expression, bool preserveInferenceMetadata) {
	return cloneExpressionTreeImpl(*this, expression, preserveInferenceMetadata);
}

CodeLine *ParseContext::createCodeLine(
	lsp::SourceFile *sourceFile, int sourceFileLineIndex, std::string text, std::vector<SourceSlice> sourceSlices
) {
	auto line = std::make_unique<CodeLine>(std::string_view{}, sourceFile);
	line->sourceFileLineIndex = sourceFileLineIndex;
	line->setOwnedText(std::move(text));
	line->sourceSlices = std::move(sourceSlices);
	CodeLine *result = line.get();
	ownedCodeLines.push_back(std::move(line));
	return result;
}

std::shared_ptr<InstantiatedSectionBody> ParseContext::cloneSectionBody(Section *section, bool preserveInferenceMetadata) {
	if (!section)
		return {};
	auto body = std::make_shared<InstantiatedSectionBody>();
	body->sourceSection = section;
	body->lineExpressions.reserve(section->codeLines.size());
	for (CodeLine *line : section->codeLines) {
		body->lineExpressions.push_back(cloneExpressionTree(line ? line->expression : nullptr, preserveInferenceMetadata));
	}
	body->childBodies.reserve(section->children.size());
	for (Section *child : section->children)
		body->childBodies.push_back(cloneSectionBody(child, preserveInferenceMetadata));
	return body;
}

ParseContext::~ParseContext() {
	std::unordered_set<Expression *> visitedFunctions(ownedClonedExpressions.begin(), ownedClonedExpressions.end());
	for (auto &line : ownedCodeLines) {
		if (line && line->expression)
			deleteExpressionTree(line->expression, visitedFunctions);
	}
	for (Expression *expression : ownedClonedExpressions)
		delete expression;
	std::unordered_set<Section *> visitedSections;
	std::unordered_set<PatternDefinition *> visitedDefinitions;
	std::unordered_set<PatternReference *> visitedReferences;
	std::unordered_set<Variable *> visitedVars;
	std::vector<Section *> sectionStack;
	if (mainSection)
		sectionStack.push_back(mainSection);

	while (!sectionStack.empty()) {
		Section *section = sectionStack.back();
		sectionStack.pop_back();
		if (!section || !visitedSections.insert(section).second)
			continue;
		for (Section *child : section->children)
			sectionStack.push_back(child);

		for (PatternDefinition *definition : section->patternDefinitions) {
			if (definition && visitedDefinitions.insert(definition).second)
				delete definition;
		}
		for (PatternReference *reference : section->patternReferences) {
			if (!reference || !visitedReferences.insert(reference).second)
				continue;
			if (reference->expression)
				deleteExpressionTree(reference->expression, visitedFunctions);
			delete reference;
		}
		for (auto &[_, variable] : section->variables) {
			if (variable && visitedVars.insert(variable).second)
				delete variable;
		}
		if (section->type == SectionType::Class) {
			auto *classSection = static_cast<ClassSection *>(section);
			if (classSection->classDefinition) {
				for (FieldDefinition &field : classSection->classDefinition->fields) {
					if (field.declaredType.typeExpression)
						deleteExpressionTree(field.declaredType.typeExpression, visitedFunctions);
					field.declaredType.typeExpression = nullptr;
				}
				delete classSection->classDefinition;
				classSection->classDefinition = nullptr;
			}
		}
	}

	for (Section *section : visitedSections)
		delete section;
	mainSection = nullptr;

	std::unordered_set<PatternTreeNode *> visitedPatternNodes;
	for (PatternTreeNode *&tree : patternTrees) {
		deletePatternTree(tree, visitedPatternNodes);
		tree = nullptr;
	}

	delete diBuilder;
	diBuilder = nullptr;
	delete static_cast<llvm::IRBuilder<> *>(llvmBuilder);
	llvmBuilder = nullptr;
	targetMachine.reset();
	delete llvmModule;
	llvmModule = nullptr;
	delete llvmContext;
	llvmContext = nullptr;
}
