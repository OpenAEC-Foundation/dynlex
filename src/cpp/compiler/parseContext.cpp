#include "parseContext.h"
#include "compileTimeValue.h"
#include "expression.h"
#include "inferenceProductHooks.h"
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
#include <chrono>
#include <iostream>
#include <iterator>
#include <unordered_set>

namespace {
void deleteExpressionTree(Expression *expression, std::unordered_set<Expression *> &visited, bool deleteNode = true) {
	if (!expression || !visited.insert(expression).second)
		return;
	for (Expression *argument : expression->arguments)
		deleteExpressionTree(argument, visited, argument != expression->ownedArgument.get());
	deleteExpressionTree(expression->dataDeclaration, visited);
	if (deleteNode)
		delete expression;
}

void deletePatternTree(PatternTreeNode *node, std::unordered_set<PatternTreeNode *> &visited) {
	if (!node || !visited.insert(node).second)
		return;
	for (auto &[_, child] : node->literalChildren)
		deletePatternTree(child, visited);
	deletePatternTree(node->argumentChild, visited);
	deletePatternTree(node->argumentPackChild, visited);
	delete node;
}
} // namespace

ParseContext::ParseContext() = default;

void ParseContext::retainInferenceDiagnostic(const Diagnostic &diagnostic) {
	if (activeDiagnosticProduct && std::ranges::none_of(*activeDiagnosticProduct, [&](const Diagnostic &prior) {
		return prior.sameEmission(diagnostic);
	}))
		activeDiagnosticProduct->push_back(diagnostic);
	if (diagnostic.level == Diagnostic::Level::Error || !diagnostic.emission || !diagnostic.emission->publication)
		return;
	using Scope = std::shared_ptr<const VariableStorageScope>;
	std::map<Scope, int, std::owner_less<Scope>> owners;
	if (incrementalPreinferenceActive) {
		requireCompilerInvariant(preinferenceDiagnosticRoot != nullptr, "speculative diagnostic lost its root query");
		owners[preinferenceDiagnosticRoot] = 0;
	}
	if (diagnostic.emission->owner)
		owners[diagnostic.emission->owner] = diagnostic.emission->traceBase;
	for (const auto &[scope, base] : diagnostic.emission->retentionScopes)
		owners.try_emplace(scope, base);
	for (const RelatedInfo &note : diagnostic.relatedInfo)
		if (note.inferenceOwner)
			owners[note.inferenceOwner] = note.inferenceOwnerBase;
	for (const auto &[owner, base] : owners) {
		auto &records = deferredInferenceDiagnostics[owner];
		if (std::ranges::none_of(records, [&](const Diagnostic &prior) {
			return prior.sameEmission(diagnostic);
		})) {
			records.push_back(diagnostic.relativeToInference(base));
			if (incrementalPreinferenceActive)
				preinferredDiagnosticsDeferred++;
		}
	}
}

void ParseContext::printDiagnostics() {
	for (Diagnostic d : diagnostics) {
		std::cerr << d.toString() << "\n";
	}
}

PatternMatch *ParseContext::match(
	PatternReference *reference, MatchOptions options, MatchDependencies *dependencies, bool countBodyResolution
) {
	requireCompilerInvariant(
		!dependencies || !options.acceptLiterals,
		"failed match dependency tracking does not support literal-acceptance ordering"
	);
	if (reuseMatcherResult) {
		if (std::optional<PatternMatch *> reused = reuseMatcherResult(reference, options, dependencies)) {
			if (countBodyResolution)
				bodyResolutionMatchCacheHits++;
			return *reused;
		}
	}
	if (countBodyResolution)
		bodyResolutionMatchComputations++;
	auto finish = [&](PatternMatch *result) {
		if (recordMatcherResult)
			recordMatcherResult(reference, options, result, dependencies);
		return result;
	};
	MatchStorage storage;
	std::vector<MatchProgress> queue;
	queue.emplace_back(this, reference, options);
	llvm::DenseMap<MatchControlState, MatchParentAlternatives *, MatchStateInfo<MatchControlState, MatchControlStateHash>>
		memoizedStates;
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
			return finish(nullptr);
		}
		MatchProgress &currentProgress = queue.back();
		if (currentProgress.parents)
			currentProgress.parents = currentProgress.parents->canonical();
		if (currentProgress.completionToPublish) {
			MatchProgress publication = std::move(currentProgress);
			queue.pop_back();
			publication.publishCompletion(storage, queue);
			continue;
		}
		auto [memoizedState, inserted] = memoizedStates.try_emplace(currentProgress.controlState(), currentProgress.parents);
		if (!inserted) {
			MatchProgress repeated = std::move(currentProgress);
			queue.pop_back();
			repeated.mergeParents(storage, memoizedState->second, queue);
			continue;
		}
		steps++;
		std::vector<PatternDefinition *> visibleDefinitions = currentProgress.visibleDefinitions();
		MatchStep matchStep = currentProgress.step(storage, visibleDefinitions);
		if (currentProgress.isComplete(visibleDefinitions))
			return finish(new PatternMatch(currentProgress.materializeMatch(storage, visibleDefinitions)));
		queue.pop_back();
		queue.insert(
			queue.end(), std::make_move_iterator(matchStep.nextMatches.begin()),
			std::make_move_iterator(matchStep.nextMatches.end())
		);
	}
	recordDependencies();
	return finish(nullptr);
}

bool ParseContext::registerShaderUniform(std::string uniformName, std::uint32_t binding, DataType type, Range range) {
	for (const auto &resource : shaderResources) {
		if (resource.binding == binding || resource.name == uniformName) {
			addDiagnostic(Diagnostic(
				*this, Diagnostic::Level::Error, "shader resource binding conflict", range, "binding", std::to_string(binding),
				"first", resource.name, "second", uniformName
			));
			return false;
		}
	}
	for (const ShaderUniform &previous : shaderUniforms) {
		if (previous.name == uniformName) {
			if (previous.binding != binding) {
				addDiagnostic(Diagnostic(
					*this, Diagnostic::Level::Error, "shader uniform binding inconsistent", range, "name", uniformName, "first",
					std::to_string(previous.binding), "second", std::to_string(binding)
				));
				return false;
			}
			if (previous.type != type) {
				addDiagnostic(
					Diagnostic(*this, Diagnostic::Level::Error, "shader uniform type inconsistent", range, "name", uniformName)
				);
				return false;
			}
			return true;
		}
		if (previous.binding == binding) {
			addDiagnostic(Diagnostic(
				*this, Diagnostic::Level::Error, "shader uniform binding duplicate", range, "binding", std::to_string(binding),
				"first", previous.name, "second", uniformName
			));
			return false;
		}
	}
	shaderUniforms.push_back({std::move(uniformName), binding, std::move(type), std::move(range)});
	return true;
}

bool ParseContext::registerShaderResource(ShaderResourceValue resource, Range range) {
	for (const auto &previous : shaderResources) {
		if (previous == resource)
			return true;
		if (previous.name == resource.name) {
			addDiagnostic(
				Diagnostic(*this, Diagnostic::Level::Error, "shader resource name inconsistent", range, "name", resource.name)
			);
			return false;
		}
		if (previous.binding == resource.binding) {
			addDiagnostic(Diagnostic(
				*this, Diagnostic::Level::Error, "shader resource binding conflict", range, "binding",
				std::to_string(resource.binding), "first", previous.name, "second", resource.name
			));
			return false;
		}
	}
	for (const auto &uniform : shaderUniforms) {
		if (uniform.binding == resource.binding || uniform.name == resource.name) {
			addDiagnostic(Diagnostic(
				*this, Diagnostic::Level::Error, "shader resource binding conflict", range, "binding",
				std::to_string(resource.binding), "first", uniform.name, "second", resource.name
			));
			return false;
		}
	}
	shaderResources.push_back(std::move(resource));
	return true;
}

void ParseContext::registerShaderVertexInput(std::string name, std::uint32_t location, DataType type, Range range) {
	shaderVertexInputs.push_back({std::move(name), location, std::move(type), std::move(range)});
}

bool ParseContext::registerShaderInterpolant(ShaderInterpolant interpolant) {
	for (const auto &previous : shaderInterpolants) {
		if (previous.name == interpolant.name) {
			if (previous.location == interpolant.location && previous.type == interpolant.type &&
				previous.interpolation == interpolant.interpolation)
				return true;
			addDiagnostic(Diagnostic(
				*this, Diagnostic::Level::Error, "shader interpolant inconsistent", interpolant.range, "name", interpolant.name
			));
			return false;
		}
		if (previous.location == interpolant.location) {
			addDiagnostic(Diagnostic(
				*this, Diagnostic::Level::Error, "shader interpolant duplicate", interpolant.range, "location",
				std::to_string(interpolant.location), "first", previous.name, "second", interpolant.name
			));
			return false;
		}
	}
	shaderInterpolants.push_back(std::move(interpolant));
	return true;
}

VariableReference *ParseContext::createVariableReference(Range range, const std::string &name) {
	ownedVariableReferences.push_back(std::make_unique<VariableReference>(std::move(range), name));
	return ownedVariableReferences.back().get();
}

namespace {
Expression *cloneExpressionTreeImpl(
	ExpressionStorage &storage, Expression *expression, bool preserveInferenceMetadata, InstantiatedSectionBody *owningBody
) {
	if (!expression)
		return nullptr;
	Expression *clone = storage.create();
	clone->kind = expression->kind;
	clone->range = expression->range;
	clone->literalValue = expression->literalValue;
	clone->variable = expression->variable;
	clone->dataDeclaration =
		cloneExpressionTreeImpl(storage, expression->dataDeclaration, preserveInferenceMetadata, owningBody);
	clone->dataSectionValues = preserveInferenceMetadata ? expression->dataSectionValues : nullptr;
	clone->deferredFunction = expression->deferredFunction;
	clone->inferredDataValue = preserveInferenceMetadata ? expression->inferredDataValue : nullptr;
	clone->inferredValueStorage = preserveInferenceMetadata ? expression->inferredValueStorage : nullptr;
	clone->owningSectionBody = owningBody;
	clone->patternMatch = expression->patternMatch;
	clone->syntheticPatternMatch = expression->syntheticPatternMatch;
	clone->patternReference = expression->patternReference;
	clone->intrinsicName = expression->intrinsicName;
	clone->inferredFlexExpansion = nullptr;
	clone->inferredPointerStorage = nullptr;
	clone->memoryFacts = {};
	clone->inferredFlexBody = preserveInferenceMetadata ? expression->inferredFlexBody : nullptr;
	clone->inferredCallerBody = preserveInferenceMetadata ? expression->inferredCallerBody : nullptr;
	clone->staticLoopBodies = preserveInferenceMetadata ? expression->staticLoopBodies : std::nullopt;
	clone->inferredArgumentReference = preserveInferenceMetadata ? expression->inferredArgumentReference : std::nullopt;
	clone->sectionOutcome = preserveInferenceMetadata ? expression->sectionOutcome : CopyOnWrite<Expression::SectionOutcome>{};
	clone->executionFallsThrough = preserveInferenceMetadata ? expression->executionFallsThrough : std::nullopt;
	clone->sectionBodyReachable = !preserveInferenceMetadata || expression->sectionBodyReachable;
	clone->sectionBodyExecution =
		preserveInferenceMetadata ? expression->sectionBodyExecution : Expression::SectionBodyExecution::None;
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
	clone->subjectPlace = nullptr;
	clone->compileTimeValue = preserveInferenceMetadata ? expression->compileTimeValue : CompileTimeValue{};
	clone->arguments.reserve(expression->arguments.size());
	for (Expression *argument : expression->arguments)
		clone->arguments.push_back(cloneExpressionTreeImpl(storage, argument, preserveInferenceMetadata, owningBody));
	return clone;
}
} // namespace

ExpressionStorage::ExpressionStorage(std::shared_ptr<ExpressionAllocationStats> stats) : allocationStats(std::move(stats)) {}
ExpressionStorage::~ExpressionStorage() { allocationStats->liveExpressions -= expressions.size(); }
Expression *ExpressionStorage::create() {
	expressions.push_back(std::make_unique<Expression>());
	allocationStats->liveExpressions++;
	allocationStats->peakExpressions = std::max(allocationStats->peakExpressions, allocationStats->liveExpressions);
	return expressions.back().get();
}

void ParseContext::retainCapturedExpressionBody(Expression *expression) {
	for (auto *body = expression->owningSectionBody; body; body = body->parentBody)
		capturedExpressionBodies.try_emplace(body, body->shared_from_this());
}

std::shared_ptr<ExpressionStorage> ParseContext::createExpressionStorage() {
	return std::make_shared<ExpressionStorage>(expressionAllocationStats);
}

Expression *
ParseContext::cloneExpressionTree(Expression *expression, ExpressionStorage &storage, bool preserveInferenceMetadata) {
	return cloneExpressionTreeImpl(
		storage, expression, preserveInferenceMetadata, expression ? expression->owningSectionBody : nullptr
	);
}

CodeLine *ParseContext::createBorrowedSourceLine(lsp::SourceFile *sourceFile, int sourceFileLineIndex, std::string_view text) {
	auto line = std::make_unique<CodeLine>(text, sourceFile);
	line->sourceFileLineIndex = sourceFileLineIndex;
	line->sourceSlices.push_back({0, static_cast<int>(text.size()), sourceFile, sourceFileLineIndex, 0});
	CodeLine *result = line.get();
	ownedCodeLines.push_back(std::move(line));
	return result;
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

std::shared_ptr<InstantiatedSectionBody> ParseContext::cloneSectionBody(
	Section *section, bool preserveInferenceMetadata, InstantiatedSectionBody *parentBody,
	std::shared_ptr<const VariableStorageScope> storageScope
) {
	if (!section)
		return {};
	if (demandBodyResolution)
		demandBodyResolution(section);
	auto cloneStarted = std::chrono::steady_clock::now();
	sourceBodyClones++;
	if (!parentBody && !section->isFlex &&
		(section->type == SectionType::Function || section->type == SectionType::Retain ||
		 section->type == SectionType::Release))
		sourceFunctionBodyClones++;
	auto body = std::make_shared<InstantiatedSectionBody>();
	body->sourceSection = section;
	body->storageScope = storageScope ? std::move(storageScope) : std::make_shared<VariableStorageScope>();
	body->parentBody = parentBody;
	body->expressions = createExpressionStorage();
	body->lineExpressions.reserve(section->codeLines.size());
	for (CodeLine *line : section->codeLines) {
		body->lineExpressions.push_back(cloneExpressionTreeImpl(
			*body->expressions, line ? line->expression : nullptr, preserveInferenceMetadata, body.get()
		));
	}
	sourceBodyExpressionsCloned += body->expressions->expressions.size();
	sourceBodyCloneMilliseconds +=
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cloneStarted).count();
	body->childBodies.reserve(section->children.size());
	for (Section *child : section->children)
		body->childBodies.push_back(cloneSectionBody(child, preserveInferenceMetadata, body.get(), body->storageScope));
	return body;
}

std::shared_ptr<const VariableStorageScope>
ParseContext::storageScopeForInstantiation(Section *section, const InstantiationKey &key) {
	auto identity = inferenceProducts ? inferenceProducts->storageIdentity(section, key) : std::nullopt;
	auto [entry, inserted] = instantiationStorageScopes.try_emplace({section, key});
	if (identity) {
		auto [logical, fresh] = logicalInstantiationScopes.try_emplace(*identity);
		if (fresh)
			logical->second = !inserted && !frozenStorageIdentities.contains(entry->second.get())
								  ? entry->second
								  : std::make_shared<VariableStorageScope>();
		entry->second = logical->second;
		frozenStorageIdentities[entry->second.get()] = *identity;
	} else if (inserted || (inferenceProducts && frozenStorageIdentities.contains(entry->second.get())))
		entry->second = std::make_shared<VariableStorageScope>();
	return entry->second;
}

void ParseContext::refineInstantiationStorageScope(
	Section *section, const InstantiationKey &before, const InstantiationKey &after
) {
	auto scope = instantiationStorageScopes.extract({section, before});
	requireCompilerInvariant(!scope.empty(), "refined instantiation has no storage scope");
	if (auto previous = frozenStorageIdentities.find(scope.mapped().get()); previous != frozenStorageIdentities.end()) {
		logicalInstantiationScopes.erase(previous->second);
		frozenStorageIdentities.erase(previous);
	}
	if (auto identity = inferenceProducts ? inferenceProducts->storageIdentity(section, after) : std::nullopt) {
		logicalInstantiationScopes[*identity] = scope.mapped();
		frozenStorageIdentities[scope.mapped().get()] = *identity;
	}
	instantiationStorageScopes.insert_or_assign({section, after}, std::move(scope.mapped()));
}

std::shared_ptr<const VariableStorageScope> ParseContext::storageScopeForInvocation(
	const Expression *expression, size_t iteration, std::shared_ptr<const VariableStorageScope> parentScope
) {
	requireCompilerInvariant(expression != nullptr, "storage invocation requires an expression");
	if (!parentScope && expression->owningSectionBody)
		parentScope = expression->owningSectionBody->storageScope;
	const Expression *source = expression->reusableTemplateExpression ? expression->reusableTemplateExpression : expression;
	auto [entry, inserted] = invocationStorageScopes.try_emplace({parentScope, source, iteration});
	if (inserted)
		entry->second = std::make_shared<VariableStorageScope>();
	return entry->second;
}

ParseContext::~ParseContext() {
	std::unordered_set<Expression *> visitedFunctions;
	for (auto &line : ownedCodeLines) {
		if (line && line->expression.materialized())
			deleteExpressionTree(line->expression.materialized(), visitedFunctions);
	}
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
			deleteExpressionTree(reference->expression.materialized(), visitedFunctions);
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

std::vector<PatternDefinition *> ParseContext::sourceDefinitions(CodeLine *line) {
	if (deferredHeaderDefinitions)
		if (auto definitions = deferredHeaderDefinitions(line))
			return *definitions;
	PatternReference *reference = line->expression.rootReference();
	return reference && reference->match ? reference->match->matchingDefinitions : std::vector<PatternDefinition *>{};
}
