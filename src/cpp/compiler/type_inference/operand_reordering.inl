#pragma once

#include "function_inference.inl"

#include "llvm/ADT/SmallPtrSet.h"
#include <coroutine>
#include <exception>
#include <optional>
#include <stack>
#include <utility>

using ExpressionNodeSet = llvm::SmallPtrSet<Expression *, 32>;

static bool collectGroupingNodes(Expression *expr, ExpressionNodeSet &nodes, ExpressionNodeSet &visiting) {
	if (visiting.contains(expr))
		return true;
	if (!nodes.insert(expr).second)
		return false;
	visiting.insert(expr);
	for (Expression *arg : unconvertedExpression(expr)->arguments) {
		if (collectGroupingNodes(arg, nodes, visiting))
			return true;
	}
	visiting.erase(expr);
	return false;
}

// Completed tree walks must release their inline node-set storage before
// recursive inference begins in the caller.
[[gnu::noinline]] static bool collectGroupingNodes(Expression *expr, ExpressionNodeSet &nodes) {
	ExpressionNodeSet visiting;
	return collectGroupingNodes(expr, nodes, visiting);
}

static void recomputeRanges(Expression *expr, ExpressionNodeSet &visited) {
	if (!visited.insert(expr).second)
		return;
	for (Expression *arg : expr->arguments)
		recomputeRanges(arg, visited);
	if (expr->kind == Expression::Kind::PatternCall && !expr->arguments.empty()) {
		bool allArgumentsHaveSourceRanges = std::all_of(expr->arguments.begin(), expr->arguments.end(), [](Expression *arg) {
			return arg && arg->range.line && !arg->range.subString.empty();
		});
		if (!allArgumentsHaveSourceRanges)
			return;
		int originalStart = expr->range.start();
		int originalEnd = expr->range.end();
		int minStart = expr->arguments.front()->range.start();
		int maxEnd = expr->arguments.front()->range.end();
		for (Expression *arg : expr->arguments) {
			minStart = std::min(minStart, arg->range.start());
			maxEnd = std::max(maxEnd, arg->range.end());
		}
		if (!startsWithArgument(expr))
			minStart = originalStart;
		if (!endsWithArgument(expr))
			maxEnd = originalEnd;
		expr->range = Range(expr->range.line, minStart, maxEnd);
	}
}

static void recomputeRanges(Expression *expr) {
	ExpressionNodeSet visited;
	recomputeRanges(expr, visited);
}

static bool expressionContains(Expression *root, Expression *target, ExpressionNodeSet &visited) {
	if (root == target)
		return true;
	if (!visited.insert(root).second)
		return false;
	for (Expression *arg : unconvertedExpression(root)->arguments) {
		if (expressionContains(arg, target, visited))
			return true;
	}
	return false;
}

[[gnu::noinline]] static bool expressionContains(Expression *root, Expression *target) {
	ExpressionNodeSet visited;
	return expressionContains(root, target, visited);
}

static void resetExpressionTypes(Expression *expr, ExpressionNodeSet &visited) {
	if (!visited.insert(expr).second)
		return;
	// A new inference trial chooses conversions again from the authored tree.
	if (isInsertedConversionCall(expr)) {
		Expression original = *unconvertedExpression(expr);
		*expr = std::move(original);
	}
	auto inferredFlexBody = std::move(expr->inferredFlexBody);
	auto valueStorage = std::move(expr->inferredValueStorage);
	Expression *inferredFlexExpansion = expr->inferredFlexExpansion;
	if (expr->kind != Expression::Kind::Literal && expr->kind != Expression::Kind::TypedPlaceholder)
		expr->type = {};
	expr->compileTimeValue = {};
	expr->memoryFacts = {};
	expr->selectedPatternDefinition = nullptr;
	expr->selectedPatternPathIndex = std::nullopt;
	expr->selectedCallableDefinition = nullptr;
	expr->selectedCallablePathIndex = std::nullopt;
	expr->selectedInstantiation = nullptr;
	expr->subjectSetter = nullptr;
	expr->inferredArgumentReference.reset();
	expr->inferredCallerBody.reset();
	expr->staticLoopBodies.reset();
	expr->sectionOutcome.reset();
	expr->executionFallsThrough.reset();
	expr->sectionBodyReachable = true;
	expr->sectionBodyExecution = Expression::SectionBodyExecution::None;
	expr->sectionBodyFallsThrough = true;
	expr->branchSelection.reset();
	expr->inferredFlexExpansion = nullptr;
	expr->inferredPointerStorage = nullptr;
	expr->inferredFlexBody.reset();
	expr->dataSectionValues.reset();
	expr->inferredDataValue = nullptr;
	for (Expression *arg : expr->arguments)
		resetExpressionTypes(arg, visited);
	if (inferredFlexExpansion)
		resetExpressionTypes(inferredFlexExpansion, visited);
}

static void resetExpressionTypes(Expression *expr) {
	ExpressionNodeSet visited;
	resetExpressionTypes(expr, visited);
}

static bool expressionNeedsGroupingParens(Expression *expr) {
	return expr && (expr->kind == Expression::Kind::PatternCall || expr->kind == Expression::Kind::IntrinsicCall) &&
		   !expr->arguments.empty();
}

static std::string renderResolvedExpression(Expression *expr);

static std::string renderResolvedExpressionArgument(Expression *expr) {
	std::string rendered = renderResolvedExpression(expr);
	if (expressionNeedsGroupingParens(expr))
		return "(" + rendered + ")";
	return rendered;
}

static std::string renderResolvedExpression(Expression *expr) {
	if (isInsertedConversionCall(expr))
		return renderResolvedExpression(unconvertedExpression(expr));
	if (!expr->range.subString.empty() && (expr->kind == Expression::Kind::Literal ||
										   expr->kind == Expression::Kind::Variable || expr->kind == Expression::Kind::Pending))
		return (std::string)expr->range.subString;
	switch (expr->kind) {
	case Expression::Kind::Literal:
		if (const auto *integer = std::get_if<std::int64_t>(&expr->literalValue))
			return std::to_string(*integer);
		if (std::holds_alternative<MinimumSignedIntegerMagnitude>(expr->literalValue))
			return "9223372036854775808";
		if (const auto *number = std::get_if<double>(&expr->literalValue))
			return std::to_string(*number);
		if (const auto *text = std::get_if<std::string>(&expr->literalValue))
			return "\"" + *text + "\"";
		return "<literal>";
	case Expression::Kind::ArgumentPack:
		return std::string(expr->range.subString);
	case Expression::Kind::ArrayLiteral: {
		std::string rendered = "[";
		for (size_t i = 0; i < expr->arguments.size(); i++) {
			if (i > 0)
				rendered += ", ";
			rendered += renderResolvedExpression(expr->arguments[i]);
		}
		rendered += "]";
		return rendered;
	}
	case Expression::Kind::Variable:
		return !expr->range.subString.empty() ? (std::string)expr->range.subString : "<variable>";
	case Expression::Kind::IntrinsicCall: {
		std::string rendered = "@intrinsic(\"" + expr->intrinsicName + "\"";
		for (Expression *arg : expr->arguments)
			rendered += ", " + renderResolvedExpression(arg);
		rendered += ")";
		return rendered;
	}
	case Expression::Kind::Pending:
		return !expr->range.subString.empty() ? (std::string)expr->range.subString : "<pending>";
	case Expression::Kind::TypedPlaceholder:
		return "<typed placeholder>";
	case Expression::Kind::PatternCall:
		break;
	}

	if (!expr->patternMatch)
		return !expr->range.subString.empty() ? (std::string)expr->range.subString : "<pattern>";

	std::string rendered;
	size_t argumentIndex = 0;
	for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
		if (node->type == PatternElement::Type::Variable) {
			if (argumentIndex < expr->arguments.size())
				rendered += renderResolvedExpressionArgument(expr->arguments[argumentIndex++]);
			else
				rendered += "$";
			continue;
		}
		rendered += node->text;
	}
	return rendered.empty() && !expr->range.subString.empty() ? (std::string)expr->range.subString : rendered;
}

enum class GroupingEnumerationProgress {
	NoCandidate,
	EmittedContinue,
	Stop,
};

// Pull one grouping choice at a time. Coroutine frames retain enumeration state
// without retaining the native call stack while the candidate is validated.
template <typename T> class GroupingGenerator {
  public:
	// The standard coroutine protocol requires these exact snake_case names.
	// NOLINTBEGIN(readability-identifier-naming)
	struct promise_type;
	using Handle = std::coroutine_handle<promise_type>;

	struct promise_type {
		std::optional<T> currentValue;
		std::exception_ptr exception;

		GroupingGenerator get_return_object() { return GroupingGenerator(Handle::from_promise(*this)); }
		std::suspend_always initial_suspend() noexcept { return {}; }
		std::suspend_always final_suspend() noexcept { return {}; }
		std::suspend_always yield_value(T value) {
			currentValue = std::move(value);
			return {};
		}
		void return_void() noexcept {}
		void unhandled_exception() { exception = std::current_exception(); }
	};
	// NOLINTEND(readability-identifier-naming)

	GroupingGenerator(const GroupingGenerator &) = delete;
	GroupingGenerator &operator=(const GroupingGenerator &) = delete;
	GroupingGenerator(GroupingGenerator &&other) noexcept : handle(std::exchange(other.handle, {})) {}
	GroupingGenerator &operator=(GroupingGenerator &&other) noexcept {
		if (this == &other)
			return *this;
		if (handle)
			handle.destroy();
		handle = std::exchange(other.handle, {});
		return *this;
	}
	~GroupingGenerator() {
		if (handle)
			handle.destroy();
	}

	bool next() {
		if (!handle || handle.done())
			return false;
		handle.promise().currentValue.reset();
		handle.resume();
		if (handle.promise().exception)
			std::rethrow_exception(handle.promise().exception);
		return !handle.done();
	}

	const T &current() const { return handle.promise().currentValue.value(); }

  private:
	explicit GroupingGenerator(Handle handle) : handle(handle) {}
	Handle handle;
};

static GroupingEnumerationProgress
mergeGroupingEnumerationProgress(GroupingEnumerationProgress current, GroupingEnumerationProgress next) {
	if (next == GroupingEnumerationProgress::Stop)
		return GroupingEnumerationProgress::Stop;
	if (next == GroupingEnumerationProgress::EmittedContinue)
		return GroupingEnumerationProgress::EmittedContinue;
	return current;
}

struct GroupingFailure {
	Diagnostic diagnostic;
	bool hasDiagnostic = false;
	int priority = -1;
};

static void considerGroupingFailure(GroupingFailure *currentBest, Diagnostic diagnostic, int priority) {
	if (!currentBest)
		return;
	if (currentBest->priority >= priority)
		return;
	currentBest->diagnostic = std::move(diagnostic);
	currentBest->hasDiagnostic = true;
	currentBest->priority = priority;
}

static void captureGroupingSnapshot(Expression *expr, GroupingSnapshot &snapshot, ExpressionNodeSet &visited) {
	if (!expr || !visited.insert(expr).second)
		return;
	Expression *syntax = unconvertedExpression(expr);
	snapshot.nodes.emplace(expr, GroupingSnapshot::NodeState{syntax->arguments, syntax->isExplicitGroup});
	for (Expression *arg : syntax->arguments)
		captureGroupingSnapshot(arg, snapshot, visited);
}

[[gnu::noinline]] static GroupingSnapshot captureGroupingSnapshot(Expression *expr) {
	GroupingSnapshot snapshot;
	snapshot.root = expr;
	ExpressionNodeSet visited;
	captureGroupingSnapshot(expr, snapshot, visited);
	return snapshot;
}

static bool expressionNodeSetsEqual(const ExpressionNodeSet &left, const ExpressionNodeSet &right) {
	return left.size() == right.size() && std::all_of(left.begin(), left.end(), [&](Expression *expression) {
		return right.contains(expression);
	});
}

static void applyGroupingSnapshot(const GroupingSnapshot &snapshot) {
	for (const auto &[expression, state] : snapshot.nodes) {
		Expression *syntax = unconvertedExpression(expression);
		syntax->arguments = state.arguments;
		syntax->isExplicitGroup = state.explicitGroup;
	}
}

static bool expressionHasGroupingShape(Expression *expression) {
	expression = unconvertedExpression(expression);
	if (expression->kind == Expression::Kind::PatternCall && !expression->arguments.empty())
		return true;
	return !expression->groupingArgumentIndices.empty();
}

static size_t groupingArgumentCount(Expression *expression) {
	expression = unconvertedExpression(expression);
	if (expression->kind == Expression::Kind::PatternCall)
		return expression->arguments.size();
	return expression->groupingArgumentIndices.size();
}

static int groupingArgumentIndex(Expression *expression, size_t sourceArgumentIndex) {
	expression = unconvertedExpression(expression);
	if (!expression)
		return -1;
	if (expression->kind == Expression::Kind::PatternCall)
		return static_cast<int>(sourceArgumentIndex);
	if (sourceArgumentIndex >= expression->groupingArgumentIndices.size())
		return -1;
	return expression->groupingArgumentIndices[sourceArgumentIndex];
}

static bool startsWithArgument(Expression *expression);
static bool endsWithArgument(Expression *expression);
static bool argumentHasAdjacentSiblingSlot(Expression *expression, size_t argumentIndex);

static const GroupingSnapshot *codeLineGroupingForContext(CodeLine *line, const InferenceContext &context) {
	if (!line)
		return nullptr;
	if (line->hasCommittedGrouping && line->groupingAmbiguityChecked)
		return &line->committedGrouping;
	if (context.trial) {
		auto trialGrouping = context.trialCodeLineGroupings.find(line);
		if (trialGrouping != context.trialCodeLineGroupings.end())
			return &trialGrouping->second.grouping;
	}
	return line->hasCommittedGrouping ? &line->committedGrouping : nullptr;
}

static GroupingSnapshot groupingForReusableInstance(const GroupingSnapshot &templateGrouping, Expression *instanceRoot) {
	GroupingSnapshot instanceGrouping;
	ExpressionNodeSet instanceNodes;
	requireCompilerInvariant(
		instanceRoot && !collectGroupingNodes(instanceRoot, instanceNodes),
		"reusable grouping instance contains an expression cycle"
	);
	std::unordered_map<Expression *, Expression *> instanceByTemplate;
	for (Expression *instance : instanceNodes) {
		Expression *templateExpression = instance->reusableTemplateExpression;
		requireCompilerInvariant(templateExpression, "reusable grouping instance contains a node without a template");
		requireCompilerInvariant(
			instanceByTemplate.emplace(templateExpression, instance).second,
			"reusable grouping instance maps multiple nodes to one template"
		);
	}
	auto findInstance = [&](Expression *templateExpression) {
		auto instance = instanceByTemplate.find(templateExpression);
		requireCompilerInvariant(instance != instanceByTemplate.end(), "reusable grouping template node has no instance");
		return instance->second;
	};
	instanceGrouping.root = findInstance(templateGrouping.root);
	for (const auto &[templateExpression, templateState] : templateGrouping.nodes) {
		std::vector<Expression *> instanceArguments;
		instanceArguments.reserve(templateState.arguments.size());
		for (Expression *templateArgument : templateState.arguments)
			instanceArguments.push_back(findInstance(templateArgument));
		instanceGrouping.nodes.emplace(
			findInstance(templateExpression),
			GroupingSnapshot::NodeState{std::move(instanceArguments), templateState.explicitGroup}
		);
	}
	return instanceGrouping;
}

static void applyCodeLineGrouping(CodeLine *line, Expression *&activeExpression, const InferenceContext &context) {
	if (context.trial) {
		auto trialGrouping = context.trialCodeLineGroupings.find(line);
		if (trialGrouping != context.trialCodeLineGroupings.end()) {
			GroupingSnapshot instanceGrouping;
			const GroupingSnapshot *grouping = &trialGrouping->second.grouping;
			if (trialGrouping->second.reusableTemplate && activeExpression && activeExpression->reusableTemplateExpression) {
				instanceGrouping = groupingForReusableInstance(*grouping, activeExpression);
				grouping = &instanceGrouping;
			}
			applyGroupingSnapshot(*grouping);
			activeExpression = grouping->root;
			recomputeRanges(activeExpression);
			return;
		}
	}
	const GroupingSnapshot *grouping = nullptr;
	// Instantiation trees are cloned from the already-canonical reusable
	// template. A CodeLine snapshot contains template pointers and must never be
	// applied to an instance-owned tree.
	if (!grouping && (!activeExpression || !activeExpression->reusableTemplateExpression))
		grouping = codeLineGroupingForContext(line, context);
	if (!grouping)
		return;
	applyGroupingSnapshot(*grouping);
	activeExpression = grouping->root;
	recomputeRanges(activeExpression);
}

static bool codeLineCanReuseGrouping(CodeLine *line, const InferenceContext &context) {
	if (!line)
		return false;
	if (line->hasCommittedGrouping && line->groupingAmbiguityChecked)
		return true;
	return context.trial && context.trialCodeLineGroupings.contains(line);
}

static GroupingSnapshot reusableTemplateGrouping(const GroupingSnapshot &instanceGrouping) {
	GroupingSnapshot templateGrouping;
	auto templateExpression = [](Expression *expression) {
		return expression && expression->reusableTemplateExpression ? expression->reusableTemplateExpression : expression;
	};
	templateGrouping.root = templateExpression(instanceGrouping.root);
	for (const auto &[expression, state] : instanceGrouping.nodes) {
		std::vector<Expression *> templateArguments;
		templateArguments.reserve(state.arguments.size());
		for (Expression *argument : state.arguments)
			templateArguments.push_back(templateExpression(argument));
		templateGrouping.nodes.emplace(
			templateExpression(expression), GroupingSnapshot::NodeState{std::move(templateArguments), state.explicitGroup}
		);
	}
	return templateGrouping;
}

static void commitCodeLineGrouping(CodeLine *line, Expression *&expr, InferenceContext &context, bool ambiguityChecked) {
	GroupingSnapshot grouping = captureGroupingSnapshot(expr);
	if (context.trial) {
		if (context.groupingTrialJournal)
			context.groupingTrialJournal->recordCodeLineGroupingWrite(context.trialCodeLineGroupings, line);
		auto &trialGrouping = context.trialCodeLineGroupings[line];
		trialGrouping.reusableTemplate = expr && expr->reusableTemplateExpression;
		trialGrouping.grouping = trialGrouping.reusableTemplate ? reusableTemplateGrouping(grouping) : std::move(grouping);
		trialGrouping.ambiguityChecked = trialGrouping.ambiguityChecked || ambiguityChecked;
		return;
	}
	if (expr && expr->reusableTemplateExpression) {
		GroupingSnapshot templateGrouping = reusableTemplateGrouping(grouping);
		applyGroupingSnapshot(templateGrouping);
		line->expression = templateGrouping.root;
		recomputeRanges(line->expression);
		line->committedGrouping = captureGroupingSnapshot(line->expression);
	} else {
		line->committedGrouping = std::move(grouping);
		line->expression = line->committedGrouping.root;
	}
	line->hasCommittedGrouping = true;
	if (ambiguityChecked)
		line->groupingAmbiguityChecked = true;
}

static void commitTrialCodeLineGroupings(InferenceContext &context) {
	for (auto &[line, trialGrouping] : context.trialCodeLineGroupings) {
		if (!line)
			crashCompilerBug("trial inference recorded a grouping without a code line");
		GroupingSnapshot grouping = std::move(trialGrouping.grouping);
		if (trialGrouping.reusableTemplate) {
			applyGroupingSnapshot(grouping);
			line->expression = grouping.root;
			recomputeRanges(line->expression);
			line->committedGrouping = captureGroupingSnapshot(line->expression);
		} else {
			line->committedGrouping = std::move(grouping);
			line->expression = line->committedGrouping.root;
		}
		line->hasCommittedGrouping = true;
		line->groupingAmbiguityChecked = line->groupingAmbiguityChecked || trialGrouping.ambiguityChecked;
	}
	context.trialCodeLineGroupings.clear();
}

static int countMatchedParameters(Expression *expression, PatternDefinition *definition) {
	expression = unconvertedExpression(expression);
	if (!expression || !definition || !expression->patternMatch)
		return 0;
	(void)matchingPatternPathIndices(expression->patternMatch->nodesPassed, definition);
	int count = 0;
	for (PatternTreeNode *node : expression->patternMatch->nodesPassed) {
		if (node->type == PatternElement::Type::Variable)
			count++;
	}
	return count;
}

// Operand regrouping only cares whether the matched pattern starts with an
// argument slot and/or ends with an argument slot. Interior arguments are
// surrounded by non-argument pattern elements, so they are handled on their
// own and do not participate in surrounding regrouping. For example,
// "$ $ + $" has exactly one left boundary slot and one right boundary slot:
// the middle "$" is interior even though it is also a variable element.
// There can never be multiple left or right boundary slots.
static bool startsWithArgument(Expression *expression) {
	expression = unconvertedExpression(expression);
	if (expression->kind == Expression::Kind::PatternCall)
		return expression->patternMatch->nodesPassed.front()->type == PatternElement::Type::Variable;
	return expression->groupingStartsWithArgument;
}

static bool endsWithArgument(Expression *expression) {
	expression = unconvertedExpression(expression);
	if (expression->kind == Expression::Kind::PatternCall)
		return expression->patternMatch->nodesPassed.back()->type == PatternElement::Type::Variable;
	return expression->groupingEndsWithArgument;
}

static bool argumentHasAdjacentSiblingSlot(Expression *expression, size_t argumentIndex) {
	expression = unconvertedExpression(expression);
	if (expression->kind != Expression::Kind::PatternCall) {
		return expression->groupingArgumentHasAdjacentSiblingSlot[argumentIndex];
	}
	if (!expression->patternMatch || !expression->patternMatch->matchedEndNode)
		return false;
	for (PatternDefinition *definition : expression->patternMatch->matchingDefinitions) {
		if (!definition || countMatchedParameters(expression, definition) != static_cast<int>(expression->arguments.size()))
			continue;
		size_t currentArgumentIndex = 0;
		for (size_t elementIndex = 0; elementIndex < definition->patternElements.size(); elementIndex++) {
			if (definition->patternElements[elementIndex].type != PatternElement::Type::Variable)
				continue;
			if (currentArgumentIndex != argumentIndex) {
				currentArgumentIndex++;
				continue;
			}
			bool adjacentLeft =
				elementIndex > 0 && definition->patternElements[elementIndex - 1].type == PatternElement::Type::Variable;
			bool adjacentRight = elementIndex + 1 < definition->patternElements.size() &&
								 definition->patternElements[elementIndex + 1].type == PatternElement::Type::Variable;
			return adjacentLeft || adjacentRight;
		}
	}
	return false;
}

static const PatternFamily *expressionPrecedenceFamily(Expression *expression) {
	expression = unconvertedExpression(expression);
	if (!expression || expression->kind != Expression::Kind::PatternCall || !expression->patternMatch ||
		!expression->patternMatch->matchedEndNode)
		return nullptr;
	// All candidates share the matched endpoint and therefore the same family
	// and parameter positions, regardless of their type constraints.
	const auto &definitions = expression->patternMatch->matchingDefinitions;
	requireCompilerInvariant(!definitions.empty(), "matched expression has no pattern definitions");
	PatternDefinition *definition = definitions.front();
	requireCompilerInvariant(definition->family, "operand grouping requires resolved pattern families");
	if (countMatchedParameters(expression, definition) != static_cast<int>(expression->arguments.size()))
		return nullptr;
	return definition->family;
}

static bool expressionMustEvaluateBefore(Expression *left, Expression *right) {
	const PatternFamily *leftFamily = expressionPrecedenceFamily(left);
	const PatternFamily *rightFamily = expressionPrecedenceFamily(right);
	return leftFamily && rightFamily && leftFamily->precedenceSuccessors.contains(rightFamily);
}

class GroupingInferenceTransaction {
  public:
	explicit GroupingInferenceTransaction(
		InferenceContext &context, Expression *expression, const std::unordered_set<Expression *> &fixedGroupingRoots
	)
		: context(context), recursiveInferenceObservation(context, context.currentInstantiation),
		  originalGrouping(captureGroupingSnapshot(expression)), savedTrial(context.trial),
		  savedCurrentInstantiation(context.currentInstantiation),
		  savedCurrentInstantiatedSectionBody(context.currentInstantiatedSectionBody),
		  savedSectionFlexBodyFrames(context.sectionFlexBodyFrames), savedStaticLoopFrames(context.staticLoopFrames),
		  savedActiveFlexDefinitionStack(context.activeFlexDefinitionStack),
		  savedActiveFlexExpansionKeys(context.activeFlexExpansionKeys), savedActiveFlexCallStack(context.activeFlexCallStack),
		  savedFlexCallSiteSectionStack(context.flexCallSiteSectionStack), savedKnownConstants(context.currentVariableValues),
		  savedAddressState(context.currentAddressState), savedTypeRefinements(context.typeRefinements),
		  savedSubject(context.currentSubject), savedTypesValid(context.typesValid),
		  savedSuppressDiagnostics(context.suppressDiagnostics),
		  savedSuppressReinferPassDiagnostics(context.suppressReinferPassDiagnostics),
		  savedTypeFailureDetail(context.typeFailureDetail),
		  savedConfiguredTypeFailureDetail(context.configuredTypeFailureDetail),
		  savedTypeFailureRelatedInfo(context.typeFailureRelatedInfo), savedTypeFailureSnapshot(context.typeFailureSnapshot),
		  savedTypeFailureDiagnostic(context.typeFailureDiagnostic), savedTypeFailurePriority(context.typeFailurePriority),
		  savedHasTypeFailureDiagnostic(context.hasTypeFailureDiagnostic), savedTrialJournal(context.trialJournal),
		  savedGroupingTrialJournal(context.groupingTrialJournal), savedFixedGroupingRoots(context.fixedGroupingRoots),
		  savedResolvedGroupingRoots(context.resolvedGroupingRoots),
		  savedDetectGroupingAmbiguity(context.detectGroupingAmbiguity), savedPendingDiagnostics(context.pendingDiagnostics),
		  savedExpressionStack(context.expressionStack),
		  savedInheritedTrialExpressionValues(context.inheritedTrialExpressionValues),
		  trialFixedGroupingRoots(fixedGroupingRoots),
		  trialFixedGroupingScope{trialFixedGroupingRoots, savedFixedGroupingRoots} {
		context.trial = true;
		context.trialJournal = &journal;
		context.groupingTrialJournal = &groupingJournal;
		context.detectGroupingAmbiguity = true;
		context.pendingDiagnostics = &queuedDiagnostics;
		context.fixedGroupingRoots = &trialFixedGroupingScope;
		context.resolvedGroupingRoots = &trialFixedGroupingRoots;
		context.typesValid = true;
		context.clearTypeFailure();
	}

	GroupingInferenceTransaction(const GroupingInferenceTransaction &) = delete;
	GroupingInferenceTransaction &operator=(const GroupingInferenceTransaction &) = delete;

	~GroupingInferenceTransaction() {
		if (active)
			rollback();
	}

	const std::unordered_set<Expression *> &resolvedGroupingRoots() const { return trialFixedGroupingRoots; }

	std::vector<Diagnostic> takeDiagnostics() { return std::move(queuedDiagnostics); }
	bool dependsOnRecursiveDependency() const { return recursiveInferenceObservation.ownerObserved(); }
	bool observedRecursiveDependency() const { return recursiveInferenceObservation.observed(); }

	void rollback() {
		requireCompilerInvariant(active, "grouping inference transaction was resolved twice");
		recursiveInferenceObservation.discard();
		groupingJournal.rollback();
		rollbackTrialJournal(context, journal);
		restoreSavedState();
		active = false;
	}

	void rollback(Expression *&expression) {
		rollback();
		applyGroupingSnapshot(originalGrouping);
		expression = originalGrouping.root;
		recomputeRanges(expression);
		resetExpressionTypes(expression);
	}

	void promote() {
		requireCompilerInvariant(active, "grouping inference transaction was resolved twice");
		requireCompilerInvariant(
			context.currentInstantiation == savedCurrentInstantiation,
			"grouping inference transaction changed the active function instantiation"
		);
		requireCompilerInvariant(
			context.expressionStack == savedExpressionStack,
			"grouping inference transaction left an unbalanced expression stack"
		);
		if (savedTrial) {
			requireCompilerInvariant(savedTrialJournal, "nested grouping inference transaction has no parent journal");
			savedTrialJournal->absorb(std::move(journal));
		} else {
			for (const auto &[expression, value] : context.trialExpressionValues)
				setExpressionCompileTimeValue(expression, value);
			commitTrialCodeLineGroupings(context);
			for (const auto &[definition, instantiation] : context.trialCallableInstantiations) {
				requireCompilerInvariant(
					definition && instantiation, "trial inference recorded an incomplete callable instantiation"
				);
				definition->callableInstantiation = instantiation;
			}
			context.trialExpressionValues.clear();
			context.trialCallableInstantiations.clear();
		}
		if (savedGroupingTrialJournal)
			savedGroupingTrialJournal->absorb(std::move(groupingJournal));
		restoreConfiguration();
		active = false;
	}

  private:
	InferenceContext &context;
	ScopedRecursiveInferenceObservation recursiveInferenceObservation;
	GroupingSnapshot originalGrouping;
	InferenceContext::TrialJournal journal;
	InferenceContext::GroupingTrialJournal groupingJournal;
	bool savedTrial;
	Instantiation *savedCurrentInstantiation;
	InstantiatedSectionBody *savedCurrentInstantiatedSectionBody;
	std::vector<InferenceContext::SectionFlexBodyInferenceFrame> savedSectionFlexBodyFrames;
	std::vector<InferenceContext::StaticLoopFrame> savedStaticLoopFrames;
	std::vector<Section *> savedActiveFlexDefinitionStack;
	std::vector<std::optional<FlexExpansionKey>> savedActiveFlexExpansionKeys;
	std::vector<Expression *> savedActiveFlexCallStack;
	std::vector<Section *> savedFlexCallSiteSectionStack;
	KnownConstantState savedKnownConstants;
	AddressInferenceState savedAddressState;
	TypeRefinementState savedTypeRefinements;
	InferenceContext::SubjectState savedSubject;
	bool savedTypesValid;
	bool savedSuppressDiagnostics;
	bool savedSuppressReinferPassDiagnostics;
	std::string savedTypeFailureDetail;
	std::shared_ptr<const ConfiguredDiagnosticMessage> savedConfiguredTypeFailureDetail;
	std::vector<RelatedInfo> savedTypeFailureRelatedInfo;
	DiagnosticExpressionSnapshot savedTypeFailureSnapshot;
	Diagnostic savedTypeFailureDiagnostic;
	int savedTypeFailurePriority;
	bool savedHasTypeFailureDiagnostic;
	InferenceContext::TrialJournal *savedTrialJournal;
	InferenceContext::GroupingTrialJournal *savedGroupingTrialJournal;
	const InferenceContext::FixedGroupingScope *savedFixedGroupingRoots;
	std::unordered_set<Expression *> *savedResolvedGroupingRoots;
	bool savedDetectGroupingAmbiguity;
	std::vector<Diagnostic> *savedPendingDiagnostics;
	std::vector<InferenceContext::ExpressionFrame> savedExpressionStack;
	const std::unordered_map<Expression *, CompileTimeValue> *savedInheritedTrialExpressionValues;
	std::unordered_set<Expression *> trialFixedGroupingRoots;
	InferenceContext::FixedGroupingScope trialFixedGroupingScope;
	std::vector<Diagnostic> queuedDiagnostics;
	bool active = true;

	void restoreConfiguration() {
		context.trial = savedTrial;
		context.currentInstantiation = savedCurrentInstantiation;
		context.trialJournal = savedTrialJournal;
		context.groupingTrialJournal = savedGroupingTrialJournal;
		context.fixedGroupingRoots = savedFixedGroupingRoots;
		context.resolvedGroupingRoots = savedResolvedGroupingRoots;
		context.detectGroupingAmbiguity = savedDetectGroupingAmbiguity;
		context.pendingDiagnostics = savedPendingDiagnostics;
		context.inheritedTrialExpressionValues = savedInheritedTrialExpressionValues;
	}

	void restoreSavedState() {
		context.currentInstantiatedSectionBody = savedCurrentInstantiatedSectionBody;
		context.sectionFlexBodyFrames = std::move(savedSectionFlexBodyFrames);
		context.staticLoopFrames = std::move(savedStaticLoopFrames);
		context.activeFlexDefinitionStack = std::move(savedActiveFlexDefinitionStack);
		context.activeFlexExpansionKeys = std::move(savedActiveFlexExpansionKeys);
		context.activeFlexCallStack = std::move(savedActiveFlexCallStack);
		context.flexCallSiteSectionStack = std::move(savedFlexCallSiteSectionStack);
		context.currentVariableValues = std::move(savedKnownConstants);
		context.currentAddressState = std::move(savedAddressState);
		context.typeRefinements = std::move(savedTypeRefinements);
		context.currentSubject = savedSubject;
		context.typesValid = savedTypesValid;
		context.suppressDiagnostics = savedSuppressDiagnostics;
		context.suppressReinferPassDiagnostics = savedSuppressReinferPassDiagnostics;
		context.typeFailureDetail = std::move(savedTypeFailureDetail);
		context.configuredTypeFailureDetail = std::move(savedConfiguredTypeFailureDetail);
		context.typeFailureRelatedInfo = std::move(savedTypeFailureRelatedInfo);
		context.typeFailureSnapshot = savedTypeFailureSnapshot;
		context.typeFailureDiagnostic = std::move(savedTypeFailureDiagnostic);
		context.typeFailurePriority = savedTypeFailurePriority;
		context.hasTypeFailureDiagnostic = savedHasTypeFailureDiagnostic;
		context.expressionStack = std::move(savedExpressionStack);
		restoreConfiguration();
	}
};

static bool standaloneExpressionHasNonVoidResult(
	Expression *expression, const InferenceContext &context, bool observedRecursiveDependency
) {
	DataType resultType = expression ? expression->type : DataType{};
	if (expression && expression->dataDeclaration)
		return false;
	if (resultType.isDeduced())
		return resultType.kind != DataType::Kind::Void;
	requireCompilerInvariant(
		observedRecursiveDependency && context.currentInstantiation && context.currentInstantiation->needsReinfer,
		"standalone expression remained undeduced without a pending recursive reinference"
	);
	return false;
}

static bool validateGroupingInTrial(
	Expression *expr, InferenceContext &context, const std::unordered_set<Expression *> &fixedGroupingRoots,
	const BindingFrameStack &flexBindingFrameStack, const DiagnosticExpressionSnapshot &failureSnapshot,
	bool requireVoidResult = false, GroupingFailure *trialFailure = nullptr,
	std::unordered_set<Expression *> *resolvedGroupingRoots = nullptr, GroupingSnapshot *resolvedGroupingSnapshot = nullptr,
	std::vector<Diagnostic> *resolvedDiagnostics = nullptr,
	std::unique_ptr<GroupingInferenceTransaction> *retainedTransaction = nullptr
) {
	ExpressionNodeSet originalExpressionNodes;
	if (collectGroupingNodes(expr, originalExpressionNodes)) {
		considerGroupingFailure(
			trialFailure, buildFailureDetailDiagnostic(failureSnapshot.range, "internal operand regrouping cycle detected"), 1
		);
		return false;
	}
	auto transaction = std::make_unique<GroupingInferenceTransaction>(context, expr, fixedGroupingRoots);
	resetExpressionTypes(expr);
	inferOrderedExpression(expr, context, flexBindingFrameStack, true);
	ExpressionNodeSet inferredExpressionNodes;
	bool inferredCycle = collectGroupingNodes(expr, inferredExpressionNodes);
	requireCompilerInvariant(
		!inferredCycle && expressionNodeSetsEqual(inferredExpressionNodes, originalExpressionNodes),
		"grouping trial inference changed the expression node set"
	);
	bool trialSucceeded = context.typesValid;
	if (trialSucceeded && requireVoidResult) {
		if (standaloneExpressionHasNonVoidResult(expr, context, transaction->dependsOnRecursiveDependency())) {
			std::string detail = "Standalone expression '" + std::string(expr->range.subString) +
								 "' must return nothing; use ignore if you want to ignore a value";
			Diagnostic diagnostic = buildFailureDetailDiagnostic(failureSnapshot.range, detail);
			context.fail(std::move(diagnostic), 0);
			considerGroupingFailure(trialFailure, context.typeFailureDiagnostic, 0);
			trialSucceeded = false;
		}
	}
	if (trialSucceeded && resolvedGroupingSnapshot)
		*resolvedGroupingSnapshot = captureGroupingSnapshot(expr);
	if (trialSucceeded && resolvedDiagnostics)
		*resolvedDiagnostics = transaction->takeDiagnostics();
	if (!trialSucceeded) {
		if (context.hasTypeFailureDiagnostic) {
			considerGroupingFailure(trialFailure, context.typeFailureDiagnostic, context.typeFailurePriority);
		} else {
			Range failureRange =
				context.typeFailureSnapshot.range.line ? context.typeFailureSnapshot.range : failureSnapshot.range;
			considerGroupingFailure(
				trialFailure,
				buildFailureDetailDiagnostic(
					failureRange, context.typeFailureDetail, context.typeFailureRelatedInfo, context.configuredTypeFailureDetail
				),
				1
			);
		}
	}
	if (trialSucceeded && resolvedGroupingRoots) {
		// Replay restores only this expression's snapshot. Callee body orderings
		// are rolled back separately and must be inferred again, not frozen.
		resolvedGroupingRoots->clear();
		for (Expression *root : transaction->resolvedGroupingRoots()) {
			if (originalExpressionNodes.contains(root))
				resolvedGroupingRoots->insert(root);
		}
	}
	if (trialSucceeded && retainedTransaction)
		*retainedTransaction = std::move(transaction);
	else
		transaction->rollback(expr);
	return trialSucceeded;
}

#include "operand_reordering_generation.inl"
#include "operand_reordering_inference.inl"
