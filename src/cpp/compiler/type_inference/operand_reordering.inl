#pragma once

#include "function_inference.inl"

#include <coroutine>
#include <exception>
#include <optional>
#include <stack>
#include <utility>

static bool expressionTreeHasCycle(Expression *expr, std::unordered_set<Expression *> &visiting) {
	if (!visiting.insert(expr).second)
		return true;
	for (Expression *arg : expr->arguments) {
		if (expressionTreeHasCycle(arg, visiting))
			return true;
	}
	visiting.erase(expr);
	return false;
}

static bool expressionTreeHasCycle(Expression *expr) {
	std::unordered_set<Expression *> visiting;
	return expressionTreeHasCycle(expr, visiting);
}

static void collectExpressionNodes(Expression *expr, std::unordered_set<Expression *> &nodes) {
	if (!expr || !nodes.insert(expr).second)
		return;
	for (Expression *argument : expr->arguments)
		collectExpressionNodes(argument, nodes);
}

static void recomputeRanges(Expression *expr, std::unordered_set<Expression *> &visited) {
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
	std::unordered_set<Expression *> visited;
	recomputeRanges(expr, visited);
}

static bool expressionContains(Expression *root, Expression *target, std::unordered_set<Expression *> &visited) {
	if (root == target)
		return true;
	if (!visited.insert(root).second)
		return false;
	for (Expression *arg : root->arguments) {
		if (expressionContains(arg, target, visited))
			return true;
	}
	return false;
}

static bool expressionContains(Expression *root, Expression *target) {
	std::unordered_set<Expression *> visited;
	return expressionContains(root, target, visited);
}

static void resetExpressionTypes(Expression *expr, std::unordered_set<Expression *> &visited) {
	if (!visited.insert(expr).second)
		return;
	Expression *inferredFlexExpansion = expr->inferredFlexExpansion;
	if (expr->kind != Expression::Kind::Literal && expr->kind != Expression::Kind::TypedPlaceholder)
		expr->type = {};
	expr->compileTimeValue = {};
	expr->selectedPatternDefinition = nullptr;
	expr->selectedCallableDefinition = nullptr;
	expr->selectedInstantiation = nullptr;
	expr->usesTrialInstantiationSummary = false;
	expr->branchSelection.reset();
	expr->inferredFlexExpansion = nullptr;
	expr->inferredFlexBody.reset();
	for (Expression *arg : expr->arguments)
		resetExpressionTypes(arg, visited);
	if (inferredFlexExpansion)
		resetExpressionTypes(inferredFlexExpansion, visited);
}

static void resetExpressionTypes(Expression *expr) {
	std::unordered_set<Expression *> visited;
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
	if (!expr->range.subString.empty() && (expr->kind == Expression::Kind::Literal ||
										   expr->kind == Expression::Kind::Variable || expr->kind == Expression::Kind::Pending))
		return (std::string)expr->range.subString;
	switch (expr->kind) {
	case Expression::Kind::Literal:
		if (const auto *number = std::get_if<double>(&expr->literalValue))
			return std::to_string(*number);
		if (const auto *text = std::get_if<std::string>(&expr->literalValue))
			return "\"" + *text + "\"";
		return "<literal>";
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

static std::string
buildOperandGroupingWarningKey(const Range &range, std::string_view chosenGrouping, std::string_view alternativeGrouping) {
	std::string key = std::to_string(reinterpret_cast<uintptr_t>(range.line));
	key += "|";
	key += std::to_string(range.start());
	key += "|";
	key += std::to_string(range.end());
	key += "|";
	key += chosenGrouping;
	key += "|";
	key += alternativeGrouping;
	return key;
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

static void captureGroupingSnapshot(Expression *expr, GroupingSnapshot &snapshot, std::unordered_set<Expression *> &visited) {
	if (!expr || !visited.insert(expr).second)
		return;
	snapshot.argumentsByExpression[expr] = expr->arguments;
	snapshot.explicitGroupByExpression[expr] = expr->isExplicitGroup;
	for (Expression *arg : expr->arguments)
		captureGroupingSnapshot(arg, snapshot, visited);
}

static GroupingSnapshot captureGroupingSnapshot(Expression *expr) {
	GroupingSnapshot snapshot;
	snapshot.root = expr;
	std::unordered_set<Expression *> visited;
	captureGroupingSnapshot(expr, snapshot, visited);
	return snapshot;
}

static void applyGroupingSnapshot(const GroupingSnapshot &snapshot) {
	for (const auto &[expression, arguments] : snapshot.argumentsByExpression)
		expression->arguments = arguments;
	for (const auto &[expression, explicitGroup] : snapshot.explicitGroupByExpression)
		expression->isExplicitGroup = explicitGroup;
}

static bool expressionHasGroupingShape(Expression *expression) {
	if (expression->kind == Expression::Kind::PatternCall && !expression->arguments.empty())
		return true;
	return !expression->groupingArgumentIndices.empty();
}

static size_t groupingArgumentCount(Expression *expression) {
	if (expression->kind == Expression::Kind::PatternCall)
		return expression->arguments.size();
	return expression->groupingArgumentIndices.size();
}

static int groupingArgumentIndex(Expression *expression, size_t sourceArgumentIndex) {
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

static const std::vector<Expression *> &snapshotArguments(const GroupingSnapshot &snapshot, Expression *expression) {
	static const std::vector<Expression *> emptyArguments;
	auto it = snapshot.argumentsByExpression.find(expression);
	return it != snapshot.argumentsByExpression.end() ? it->second : emptyArguments;
}

static bool snapshotsHaveSameLocalOrdering(
	const GroupingSnapshot &left, const GroupingSnapshot &right, Expression *leftExpr, Expression *rightExpr, bool isOnBoundary,
	bool isRoot, bool forceLocal
) {
	auto isOpaqueAtCurrentLevel = [&](Expression *expression, const GroupingSnapshot & /*snapshot*/) -> bool {
		if (!expressionHasGroupingShape(expression))
			return true;
		return !isRoot && (forceLocal || expression->isExplicitGroup || !isOnBoundary);
	};

	bool leftOpaque = isOpaqueAtCurrentLevel(leftExpr, left);
	bool rightOpaque = isOpaqueAtCurrentLevel(rightExpr, right);
	if (leftOpaque || rightOpaque)
		return leftOpaque && rightOpaque;
	if (leftExpr != rightExpr)
		return false;

	const std::vector<Expression *> &leftArguments = snapshotArguments(left, leftExpr);
	const std::vector<Expression *> &rightArguments = snapshotArguments(right, rightExpr);
	if (leftArguments.size() != rightArguments.size())
		return false;

	bool hasLeftEdge = startsWithArgument(leftExpr);
	bool hasRightEdge = endsWithArgument(leftExpr);
	size_t sourceArgumentCount = groupingArgumentCount(leftExpr);
	for (size_t sourceArgumentIndex = 0; sourceArgumentIndex < sourceArgumentCount; sourceArgumentIndex++) {
		int leftArgumentIndex = groupingArgumentIndex(leftExpr, sourceArgumentIndex);
		int rightArgumentIndex = groupingArgumentIndex(rightExpr, sourceArgumentIndex);
		if (leftArgumentIndex < 0 || rightArgumentIndex < 0)
			return false;
		bool isLeftBoundaryArgument = hasLeftEdge && sourceArgumentIndex == 0;
		bool isRightBoundaryArgument = hasRightEdge && sourceArgumentIndex + 1 == sourceArgumentCount;
		bool childForceLocal = argumentHasAdjacentSiblingSlot(leftExpr, sourceArgumentIndex);
		if (!snapshotsHaveSameLocalOrdering(
				left, right, leftArguments[leftArgumentIndex], rightArguments[rightArgumentIndex],
				isLeftBoundaryArgument || isRightBoundaryArgument, false, childForceLocal
			))
			return false;
	}
	return true;
}

static bool snapshotsHaveSameLocalOrdering(const GroupingSnapshot &left, const GroupingSnapshot &right) {
	return snapshotsHaveSameLocalOrdering(left, right, left.root, right.root, true, true, false);
}

static const GroupingSnapshot *codeLineGroupingForContext(CodeLine *line, const InferenceContext &context) {
	if (!line)
		return nullptr;
	if (line->hasCommittedGrouping && line->groupingAmbiguityChecked)
		return &line->committedGrouping;
	if (context.trial) {
		auto trialGrouping = context.trialCodeLineGroupings.find(line);
		if (trialGrouping != context.trialCodeLineGroupings.end())
			return &trialGrouping->second;
	}
	return line->hasCommittedGrouping ? &line->committedGrouping : nullptr;
}

static void applyCodeLineGrouping(CodeLine *line, Expression *&activeExpression, const InferenceContext &context) {
	const GroupingSnapshot *grouping = nullptr;
	if (context.trial) {
		auto trialGrouping = context.trialCodeLineGroupings.find(line);
		if (trialGrouping != context.trialCodeLineGroupings.end())
			grouping = &trialGrouping->second;
	}
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
	for (const auto &[expression, arguments] : instanceGrouping.argumentsByExpression) {
		auto &templateArguments = templateGrouping.argumentsByExpression[templateExpression(expression)];
		templateArguments.reserve(arguments.size());
		for (Expression *argument : arguments)
			templateArguments.push_back(templateExpression(argument));
	}
	for (const auto &[expression, isExplicitGroup] : instanceGrouping.explicitGroupByExpression)
		templateGrouping.explicitGroupByExpression[templateExpression(expression)] = isExplicitGroup;
	return templateGrouping;
}

static void commitCodeLineGrouping(CodeLine *line, Expression *&expr, InferenceContext &context, bool ambiguityChecked) {
	GroupingSnapshot grouping = captureGroupingSnapshot(expr);
	if (context.trial) {
		context.trialCodeLineGroupings[line] = std::move(grouping);
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

static int countMatchedParameters(Expression *expression, PatternDefinition *definition) {
	if (!expression || !definition || !expression->patternMatch)
		return 0;
	int count = 0;
	for (PatternTreeNode *node : expression->patternMatch->nodesPassed) {
		if (node && node->parameterNames.contains(definition))
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
	if (expression->kind == Expression::Kind::PatternCall)
		return expression->patternMatch->nodesPassed.front()->type == PatternElement::Type::Variable;
	return expression->groupingStartsWithArgument;
}

static bool endsWithArgument(Expression *expression) {
	if (expression->kind == Expression::Kind::PatternCall)
		return expression->patternMatch->nodesPassed.back()->type == PatternElement::Type::Variable;
	return expression->groupingEndsWithArgument;
}

static bool argumentHasAdjacentSiblingSlot(Expression *expression, size_t argumentIndex) {
	if (expression->kind != Expression::Kind::PatternCall) {
		return expression->groupingArgumentHasAdjacentSiblingSlot[argumentIndex];
	}
	if (!expression->patternMatch || !expression->patternMatch->matchedEndNode)
		return false;
	for (PatternDefinition *definition : expression->patternMatch->matchedEndNode->matchingDefinitions) {
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

static int expressionPrecedence(Expression *expression) {
	if (expression->kind != Expression::Kind::PatternCall)
		return expression->groupingPrecedence;
	if (!expression->patternMatch || !expression->patternMatch->matchedEndNode ||
		expression->patternMatch->matchedEndNode->matchingDefinitions.empty())
		return 0;
	int precedence = 0;
	for (PatternDefinition *def : expression->patternMatch->matchedEndNode->matchingDefinitions) {
		if (!def || countMatchedParameters(expression, def) != (int)expression->arguments.size())
			continue;
		if (def->precedence <= 0)
			continue;
		if (precedence == 0 || def->precedence < precedence)
			precedence = def->precedence;
	}
	return precedence;
}

static bool validateGroupingInTrial(
	Expression *expr, InferenceContext &context, const std::unordered_set<Expression *> &fixedGroupingRoots,
	const BindingFrameStack &flexBindingFrameStack, const DiagnosticExpressionSnapshot &failureSnapshot,
	bool requireVoidResult = false, GroupingFailure *trialFailure = nullptr,
	std::unordered_set<Expression *> *resolvedGroupingRoots = nullptr, GroupingSnapshot *resolvedGroupingSnapshot = nullptr,
	std::vector<InferenceContext::OperandGroupingWarning> *resolvedGroupingWarnings = nullptr
) {
	GroupingSnapshot originalGrouping = captureGroupingSnapshot(expr);
	std::unordered_set<Expression *> originalExpressionNodes;
	collectExpressionNodes(expr, originalExpressionNodes);
	if (expressionTreeHasCycle(expr)) {
		considerGroupingFailure(
			trialFailure, buildFailureDetailDiagnostic(failureSnapshot.range, "internal operand regrouping cycle detected"), 1
		);
		return false;
	}
	resetExpressionTypes(expr);
	InferenceContext::TrialJournal journal;
	InferenceContext trialContext(context.parseContext, true);
	trialContext.currentInstantiation = context.currentInstantiation;
	trialContext.currentKnownConstants = context.currentKnownConstants;
	trialContext.observedInProgressUndeducedInstantiation = context.observedInProgressUndeducedInstantiation;
	trialContext.inheritedTrialExpressionValues =
		context.trial ? &context.trialExpressionValues : context.inheritedTrialExpressionValues;
	trialContext.trialJournal = &journal;
	trialContext.trialInstantiationCache =
		context.trialInstantiationCache ? context.trialInstantiationCache : context.ensureTrialInstantiationCache();
	trialContext.unresolvedPatternConstraintSignal = context.unresolvedPatternConstraintSignal;
	trialContext.allowTrialSummaryReuse = true;
	// Trial validation only needs to answer "is this grouping valid?".
	// Ambiguity scanning belongs to the outer committed inference pass.
	trialContext.detectGroupingAmbiguity = false;
	std::vector<InferenceContext::OperandGroupingWarning> trialGroupingWarnings;
	trialContext.pendingOperandGroupingWarnings = &trialGroupingWarnings;
	std::unordered_set<Expression *> trialFixedGroupingRoots = fixedGroupingRoots;
	trialContext.fixedGroupingRoots = &trialFixedGroupingRoots;
	trialContext.resolvedGroupingRoots = &trialFixedGroupingRoots;
	inferOrderedExpression(expr, trialContext, flexBindingFrameStack, true);
	if (trialContext.observedInProgressUndeducedInstantiation || trialContext.groupingAmbiguityIncomplete)
		context.groupingAmbiguityIncomplete = true;
	std::unordered_set<Expression *> inferredExpressionNodes;
	collectExpressionNodes(expr, inferredExpressionNodes);
	requireCompilerInvariant(
		inferredExpressionNodes == originalExpressionNodes, "grouping trial inference changed the expression node set"
	);
	bool trialSucceeded = trialContext.typesValid;
	if (trialSucceeded && trialContext.typesValid && requireVoidResult) {
		DataType lineType = expr ? expr->type : DataType{};
		if (!lineType.isDeduced() || lineType.kind != DataType::Kind::Void) {
			std::string detail = "Standalone expression '" + std::string(expr->range.subString) +
								 "' must return nothing; use discard if you want to ignore a value";
			Diagnostic diagnostic = buildFailureDetailDiagnostic(failureSnapshot.range, detail);
			trialContext.fail(std::move(diagnostic), 0);
			considerGroupingFailure(trialFailure, trialContext.typeFailureDiagnostic, 0);
			trialSucceeded = false;
		}
	}
	if (trialSucceeded && trialContext.typesValid && resolvedGroupingSnapshot)
		*resolvedGroupingSnapshot = captureGroupingSnapshot(expr);
	if (trialSucceeded && trialContext.typesValid && resolvedGroupingWarnings)
		*resolvedGroupingWarnings = std::move(trialGroupingWarnings);
	if (!trialSucceeded || !trialContext.typesValid) {
		if (trialContext.hasTypeFailureDiagnostic) {
			considerGroupingFailure(trialFailure, trialContext.typeFailureDiagnostic, trialContext.typeFailurePriority);
		} else {
			Range failureRange =
				trialContext.typeFailureSnapshot.range.line ? trialContext.typeFailureSnapshot.range : failureSnapshot.range;
			considerGroupingFailure(
				trialFailure,
				buildFailureDetailDiagnostic(failureRange, trialContext.typeFailureDetail, trialContext.typeFailureRelatedInfo),
				1
			);
		}
	}
	if (trialSucceeded && trialContext.typesValid && resolvedGroupingRoots)
		*resolvedGroupingRoots = std::move(trialFixedGroupingRoots);
	rollbackTrialJournal(journal, trialContext.trialInstantiationCache.get());
	applyGroupingSnapshot(originalGrouping);
	expr = originalGrouping.root;
	recomputeRanges(expr);
	resetExpressionTypes(expr);
	return trialSucceeded && trialContext.typesValid;
}

struct GroupingGenerationStep {};

struct SuspendedGroupingLayout {
	Expression *root{};
	std::vector<Expression *> expressions;
	std::vector<size_t> argumentOffsets;
	std::vector<Expression *> arguments;
	std::vector<bool> explicitGroups;
};

// Candidate validation may temporarily apply another grouping to the same
// expression nodes. Keep exactly the currently suspended pointer layout so the
// generator can resume deterministically without retaining earlier candidates.
static SuspendedGroupingLayout
captureSuspendedGroupingLayout(Expression *root, const std::unordered_set<Expression *> &expressionNodes) {
	SuspendedGroupingLayout layout;
	layout.root = root;
	layout.expressions.reserve(expressionNodes.size());
	layout.argumentOffsets.reserve(expressionNodes.size() + 1);
	layout.explicitGroups.reserve(expressionNodes.size());
	size_t argumentCount = 0;
	for (Expression *expression : expressionNodes)
		argumentCount += expression->arguments.size();
	layout.arguments.reserve(argumentCount);
	for (Expression *expression : expressionNodes) {
		layout.expressions.push_back(expression);
		layout.argumentOffsets.push_back(layout.arguments.size());
		layout.arguments.insert(layout.arguments.end(), expression->arguments.begin(), expression->arguments.end());
		layout.explicitGroups.push_back(expression->isExplicitGroup);
	}
	layout.argumentOffsets.push_back(layout.arguments.size());
	return layout;
}

static void applySuspendedGroupingLayout(const SuspendedGroupingLayout &layout) {
	for (size_t expressionIndex = 0; expressionIndex < layout.expressions.size(); expressionIndex++) {
		Expression *expression = layout.expressions[expressionIndex];
		auto argumentsBegin = layout.arguments.begin() + layout.argumentOffsets[expressionIndex];
		auto argumentsEnd = layout.arguments.begin() + layout.argumentOffsets[expressionIndex + 1];
		expression->arguments.assign(argumentsBegin, argumentsEnd);
		expression->isExplicitGroup = layout.explicitGroups[expressionIndex];
	}
}

struct GroupingCandidate {
	Expression *root{};
	const std::unordered_set<Expression *> *fixedGroupingRoots{};
	const SuspendedGroupingLayout *suspendedGrouping{};
};

struct GroupingGenerationState {
	InferenceContext &context;
	const BindingFrameStack &flexBindingFrameStack;
	std::unordered_set<Expression *> fixedGroupingRoots;
	std::unordered_set<Expression *> originalExpressionNodes;
	std::vector<Expression *> flatNodes;
	std::unordered_set<Expression *> opaqueNodes;
};

static bool isOpaqueGroupingNode(Expression *expression, bool isOnBoundary, bool isRoot, bool forceLocal) {
	return !isRoot && (forceLocal || expression->isExplicitGroup || !isOnBoundary);
}

static GroupingGenerator<GroupingCandidate> generateExpressionGroupingCandidates(
	Expression *&expr, InferenceContext &context, bool alreadyOrdered, const BindingFrameStack &flexBindingFrameStack
);

static GroupingGenerator<GroupingGenerationStep> generatePrioritizedGroupingChoices(
	Expression *&current, bool isOnBoundary, bool isRoot, bool forceLocal, bool includeBoundaryArguments,
	GroupingGenerationState &state
);

static GroupingGenerator<GroupingGenerationStep>
generateOpaqueGroupingChoices(Expression *&current, GroupingGenerationState &state) {
	if (state.fixedGroupingRoots.contains(current)) {
		co_yield GroupingGenerationStep{};
		co_return;
	}

	Expression *savedCurrent = current;
	bool preserveExplicitGroup = current && current->isExplicitGroup;
	GroupingSnapshot savedCurrentSnapshot = captureGroupingSnapshot(savedCurrent);
	Expression *groupedCurrent = current;
	auto childCandidates =
		generateExpressionGroupingCandidates(groupedCurrent, state.context, false, state.flexBindingFrameStack);
	while (childCandidates.next()) {
		const GroupingCandidate &childCandidate = childCandidates.current();
		Expression *groupedExpression = childCandidate.root;
		if (preserveExplicitGroup && groupedExpression)
			groupedExpression->isExplicitGroup = true;
		current = groupedExpression;

		std::vector<Expression *> insertedRoots;
		insertedRoots.reserve(childCandidate.fixedGroupingRoots->size());
		for (Expression *root : *childCandidate.fixedGroupingRoots) {
			if (state.fixedGroupingRoots.insert(root).second)
				insertedRoots.push_back(root);
		}

		auto choices = generatePrioritizedGroupingChoices(current, true, true, false, true, state);
		while (choices.next())
			co_yield GroupingGenerationStep{};

		for (Expression *root : insertedRoots)
			state.fixedGroupingRoots.erase(root);
		applyGroupingSnapshot(savedCurrentSnapshot);
		if (preserveExplicitGroup && savedCurrent)
			savedCurrent->isExplicitGroup = true;
		groupedCurrent = savedCurrent;
		current = savedCurrent;
	}
	current = savedCurrent;
}

static GroupingGenerator<GroupingGenerationStep> generatePrioritizedGroupingArgumentChoices(
	Expression *parentExpression, bool hasLeftEdge, bool hasRightEdge, bool includeBoundaryArguments, int sourceArgumentIndex,
	GroupingGenerationState &state
) {
	if (sourceArgumentIndex < 0) {
		co_yield GroupingGenerationStep{};
		co_return;
	}

	int actualArgumentIndex = groupingArgumentIndex(parentExpression, sourceArgumentIndex);
	bool isLeftBoundaryArgument = hasLeftEdge && sourceArgumentIndex == 0;
	bool isRightBoundaryArgument =
		hasRightEdge && sourceArgumentIndex + 1 == static_cast<int>(groupingArgumentCount(parentExpression));
	if (actualArgumentIndex < 0 || (!includeBoundaryArguments && (isLeftBoundaryArgument || isRightBoundaryArgument))) {
		auto remainingChoices = generatePrioritizedGroupingArgumentChoices(
			parentExpression, hasLeftEdge, hasRightEdge, includeBoundaryArguments, sourceArgumentIndex - 1, state
		);
		while (remainingChoices.next())
			co_yield GroupingGenerationStep{};
		co_return;
	}

	Expression *savedArgument = parentExpression->arguments[actualArgumentIndex];
	Expression *argumentExpression = savedArgument;
	bool forceLocalArgument = argumentHasAdjacentSiblingSlot(parentExpression, sourceArgumentIndex);
	auto argumentChoices = generatePrioritizedGroupingChoices(
		argumentExpression, isLeftBoundaryArgument || isRightBoundaryArgument, false, forceLocalArgument, true, state
	);
	while (argumentChoices.next()) {
		parentExpression->arguments[actualArgumentIndex] = argumentExpression;
		auto remainingChoices = generatePrioritizedGroupingArgumentChoices(
			parentExpression, hasLeftEdge, hasRightEdge, includeBoundaryArguments, sourceArgumentIndex - 1, state
		);
		while (remainingChoices.next())
			co_yield GroupingGenerationStep{};
	}
	parentExpression->arguments[actualArgumentIndex] = savedArgument;
}

static GroupingGenerator<GroupingGenerationStep> generatePrioritizedGroupingChoices(
	Expression *&current, bool isOnBoundary, bool isRoot, bool forceLocal, bool includeBoundaryArguments,
	GroupingGenerationState &state
) {
	if (!expressionHasGroupingShape(current)) {
		co_yield GroupingGenerationStep{};
		co_return;
	}

	if (isOpaqueGroupingNode(current, isOnBoundary, isRoot, forceLocal)) {
		auto opaqueChoices = generateOpaqueGroupingChoices(current, state);
		while (opaqueChoices.next())
			co_yield GroupingGenerationStep{};
		co_return;
	}

	Expression *parentExpression = current;
	bool hasLeftEdge = startsWithArgument(parentExpression);
	bool hasRightEdge = endsWithArgument(parentExpression);
	auto argumentChoices = generatePrioritizedGroupingArgumentChoices(
		parentExpression, hasLeftEdge, hasRightEdge, includeBoundaryArguments,
		static_cast<int>(groupingArgumentCount(parentExpression)) - 1, state
	);
	while (argumentChoices.next())
		co_yield GroupingGenerationStep{};
}

static GroupingGenerator<GroupingGenerationStep>
generateCompletedGroupingChoices(Expression *&candidateRoot, GroupingGenerationState &state) {
	bool inserted = expressionHasGroupingShape(candidateRoot) && state.fixedGroupingRoots.insert(candidateRoot).second;
	auto choices = generatePrioritizedGroupingChoices(candidateRoot, true, true, false, true, state);
	while (choices.next())
		co_yield GroupingGenerationStep{};
	if (inserted)
		state.fixedGroupingRoots.erase(candidateRoot);
}

static bool
isEligibleGroupingRoot(int start, int end, int rootIndex, Expression *rootExpression, GroupingGenerationState &state) {
	if (!expressionHasGroupingShape(rootExpression) || state.opaqueNodes.contains(rootExpression))
		return false;
	bool hasLeftEdge = startsWithArgument(rootExpression);
	bool hasRightEdge = endsWithArgument(rootExpression);
	size_t sourceArgumentCount = groupingArgumentCount(rootExpression);
	if ((hasLeftEdge || hasRightEdge) && sourceArgumentCount == 0)
		return false;
	if (hasLeftEdge && rootIndex == start)
		return false;
	if (hasRightEdge && rootIndex == end)
		return false;
	if (!hasLeftEdge && rootIndex > start)
		return false;
	if (!hasRightEdge && rootIndex < end)
		return false;

	int rootPrecedence = expressionPrecedence(rootExpression);
	if (rootPrecedence <= 0 || !hasLeftEdge || !hasRightEdge)
		return true;
	for (int otherIndex = start; otherIndex <= end; otherIndex++) {
		if (otherIndex == rootIndex)
			continue;
		Expression *otherExpression = state.flatNodes[otherIndex];
		if (state.opaqueNodes.contains(otherExpression) || !expressionHasGroupingShape(otherExpression))
			continue;
		if (!startsWithArgument(otherExpression) || !endsWithArgument(otherExpression))
			continue;
		int otherPrecedence = expressionPrecedence(otherExpression);
		if (otherPrecedence > 0 && otherPrecedence < rootPrecedence)
			return false;
	}
	return true;
}

static GroupingGenerator<Expression *> generateFlatExpressionGroupings(int start, int end, GroupingGenerationState &state);

static GroupingGenerator<Expression *> generateRightExpressionGroupings(
	Expression *rootExpression, int rootIndex, int end, bool hasRightEdge, int rightArgumentIndex, Expression *savedRight,
	GroupingGenerationState &state
) {
	if (!hasRightEdge) {
		Expression *candidateRoot = rootExpression;
		auto choices = generatePrioritizedGroupingChoices(candidateRoot, true, true, false, true, state);
		while (choices.next())
			co_yield rootExpression;
		co_return;
	}

	auto rightGroupings = generateFlatExpressionGroupings(rootIndex + 1, end, state);
	while (rightGroupings.next()) {
		Expression *rightResult = rightGroupings.current();
		if (expressionContains(rightResult, rootExpression))
			continue;
		rootExpression->arguments[rightArgumentIndex] = rightResult;
		Expression *candidateRoot = rootExpression;
		auto choices = generatePrioritizedGroupingChoices(candidateRoot, true, true, false, true, state);
		while (choices.next())
			co_yield rootExpression;
	}
	rootExpression->arguments[rightArgumentIndex] = savedRight;
}

static GroupingGenerator<Expression *> generateFlatExpressionGroupings(int start, int end, GroupingGenerationState &state) {
	if (start > end)
		co_return;
	if (start == end) {
		Expression *singleExpression = state.flatNodes[start];
		if (state.opaqueNodes.contains(singleExpression)) {
			auto opaqueChoices = generateOpaqueGroupingChoices(singleExpression, state);
			while (opaqueChoices.next()) {
				auto completedChoices = generateCompletedGroupingChoices(singleExpression, state);
				while (completedChoices.next())
					co_yield singleExpression;
			}
			co_return;
		}
		auto completedChoices = generateCompletedGroupingChoices(singleExpression, state);
		while (completedChoices.next())
			co_yield singleExpression;
		co_return;
	}

	for (int rootIndex = end; rootIndex >= start; rootIndex--) {
		Expression *rootExpression = state.flatNodes[rootIndex];
		if (!isEligibleGroupingRoot(start, end, rootIndex, rootExpression, state))
			continue;

		bool hasLeftEdge = startsWithArgument(rootExpression);
		bool hasRightEdge = endsWithArgument(rootExpression);
		size_t sourceArgumentCount = groupingArgumentCount(rootExpression);
		int leftArgumentIndex = hasLeftEdge ? groupingArgumentIndex(rootExpression, 0) : -1;
		int rightArgumentIndex = hasRightEdge ? groupingArgumentIndex(rootExpression, sourceArgumentCount - 1) : -1;
		Expression *savedLeft = leftArgumentIndex >= 0 ? rootExpression->arguments[leftArgumentIndex] : nullptr;
		Expression *savedRight = rightArgumentIndex >= 0 ? rootExpression->arguments[rightArgumentIndex] : nullptr;
		bool inserted = state.fixedGroupingRoots.insert(rootExpression).second;

		if (hasLeftEdge) {
			auto leftGroupings = generateFlatExpressionGroupings(start, rootIndex - 1, state);
			while (leftGroupings.next()) {
				Expression *leftResult = leftGroupings.current();
				if (expressionContains(leftResult, rootExpression))
					continue;
				rootExpression->arguments[leftArgumentIndex] = leftResult;
				auto rightGroupings = generateRightExpressionGroupings(
					rootExpression, rootIndex, end, hasRightEdge, rightArgumentIndex, savedRight, state
				);
				while (rightGroupings.next())
					co_yield rightGroupings.current();
				rootExpression->arguments[leftArgumentIndex] = savedLeft;
			}
		} else {
			auto rightGroupings = generateRightExpressionGroupings(
				rootExpression, rootIndex, end, hasRightEdge, rightArgumentIndex, savedRight, state
			);
			while (rightGroupings.next())
				co_yield rightGroupings.current();
		}

		if (leftArgumentIndex >= 0)
			rootExpression->arguments[leftArgumentIndex] = savedLeft;
		if (rightArgumentIndex >= 0)
			rootExpression->arguments[rightArgumentIndex] = savedRight;
		if (inserted)
			state.fixedGroupingRoots.erase(rootExpression);
	}
}

static size_t collectFlatGroupingNodes(Expression *expr, GroupingGenerationState &state) {
	struct CollectionFrame {
		Expression *expression;
		bool isOnBoundary;
		bool isRoot;
		bool emitOperator;
	};

	std::stack<CollectionFrame> pending;
	pending.push({expr, true, true, false});
	size_t operatorCount = 0;
	while (!pending.empty()) {
		CollectionFrame frame = pending.top();
		pending.pop();
		Expression *expression = frame.expression;
		if (frame.emitOperator) {
			operatorCount++;
			state.flatNodes.push_back(expression);
			continue;
		}
		if (!expressionHasGroupingShape(expression)) {
			state.flatNodes.push_back(expression);
			continue;
		}
		if (isOpaqueGroupingNode(expression, frame.isOnBoundary, frame.isRoot, false)) {
			state.opaqueNodes.insert(expression);
			state.flatNodes.push_back(expression);
			continue;
		}

		bool hasLeftEdge = startsWithArgument(expression);
		bool hasRightEdge = endsWithArgument(expression);
		size_t sourceArgumentCount = groupingArgumentCount(expression);
		if (hasRightEdge && sourceArgumentCount > 0) {
			int rightArgumentIndex = groupingArgumentIndex(expression, sourceArgumentCount - 1);
			if (rightArgumentIndex >= 0)
				pending.push({expression->arguments[rightArgumentIndex], true, false, false});
		}
		pending.push({expression, frame.isOnBoundary, frame.isRoot, true});
		if (hasLeftEdge && sourceArgumentCount > 0) {
			int leftArgumentIndex = groupingArgumentIndex(expression, 0);
			if (leftArgumentIndex >= 0)
				pending.push({expression->arguments[leftArgumentIndex], true, false, false});
		}
	}
	return operatorCount;
}

static GroupingGenerator<GroupingCandidate> generateExpressionGroupingCandidates(
	Expression *&expr, InferenceContext &context, bool alreadyOrdered, const BindingFrameStack &flexBindingFrameStack
) {
	if (expressionTreeHasCycle(expr)) {
		if (!context.hasTypeFailureDiagnostic)
			context.fail(
				buildFailureDetailDiagnostic(
					captureDiagnosticExpressionSnapshot(expr).range, "internal operand regrouping cycle detected"
				),
				1
			);
		co_return;
	}

	GroupingGenerationState state{context, flexBindingFrameStack, {}, {}, {}, {}};
	collectExpressionNodes(expr, state.originalExpressionNodes);
	recomputeRanges(expr);
	if (alreadyOrdered) {
		bool inserted = expressionHasGroupingShape(expr) && state.fixedGroupingRoots.insert(expr).second;
		SuspendedGroupingLayout suspendedGrouping = captureSuspendedGroupingLayout(expr, state.originalExpressionNodes);
		co_yield GroupingCandidate{expr, &state.fixedGroupingRoots, &suspendedGrouping};
		applySuspendedGroupingLayout(suspendedGrouping);
		expr = suspendedGrouping.root;
		if (inserted)
			state.fixedGroupingRoots.erase(expr);
		co_return;
	}

	if (collectFlatGroupingNodes(expr, state) > 8) {
		if (!context.hasTypeFailureDiagnostic)
			context.fail(
				buildFailureDetailDiagnostic(
					captureDiagnosticExpressionSnapshot(expr).range, "too many ambiguous operand groupings"
				),
				1
			);
		co_return;
	}

	auto groupings = generateFlatExpressionGroupings(0, static_cast<int>(state.flatNodes.size()) - 1, state);
	while (groupings.next()) {
		Expression *candidateRoot = groupings.current();
		expr = candidateRoot;
		std::unordered_set<Expression *> candidateExpressionNodes;
		collectExpressionNodes(expr, candidateExpressionNodes);
		requireCompilerInvariant(
			candidateExpressionNodes == state.originalExpressionNodes, "operand regrouping changed the expression node set"
		);
		recomputeRanges(expr);
		bool inserted = expressionHasGroupingShape(expr) && state.fixedGroupingRoots.insert(expr).second;
		SuspendedGroupingLayout suspendedGrouping = captureSuspendedGroupingLayout(expr, state.originalExpressionNodes);
		co_yield GroupingCandidate{expr, &state.fixedGroupingRoots, &suspendedGrouping};
		applySuspendedGroupingLayout(suspendedGrouping);
		expr = suspendedGrouping.root;
		if (inserted)
			state.fixedGroupingRoots.erase(candidateRoot);
	}
}

static GroupingEnumerationProgress enumerateExpressionGroupings(
	Expression *&expr, InferenceContext &context, bool alreadyOrdered, const BindingFrameStack &flexBindingFrameStack,
	const std::function<GroupingEnumerationProgress(Expression *&, const std::unordered_set<Expression *> &)> &onCandidate
) {
	GroupingEnumerationProgress result = GroupingEnumerationProgress::NoCandidate;
	auto candidates = generateExpressionGroupingCandidates(expr, context, alreadyOrdered, flexBindingFrameStack);
	while (candidates.next()) {
		const GroupingCandidate &candidate = candidates.current();
		expr = candidate.root;
		GroupingEnumerationProgress progress = onCandidate(expr, *candidate.fixedGroupingRoots);
		result = mergeGroupingEnumerationProgress(result, progress);
		if (progress == GroupingEnumerationProgress::Stop)
			return GroupingEnumerationProgress::Stop;
		applySuspendedGroupingLayout(*candidate.suspendedGrouping);
		expr = candidate.suspendedGrouping->root;
		recomputeRanges(expr);
	}
	return result;
}

static bool inferExpression(
	Expression *&expr, InferenceContext &context, bool alreadyOrdered, const BindingFrameStack &flexBindingFrameStack,
	bool requireVoidResult
) {
	GroupingSnapshot initialGrouping = captureGroupingSnapshot(expr);
	DiagnosticExpressionSnapshot originalDiagnostic = captureDiagnosticExpressionSnapshot(expr);
	std::vector<InferenceContext::OperandGroupingWarning> localGroupingWarnings;
	auto *savedPendingOperandGroupingWarnings = context.pendingOperandGroupingWarnings;
	bool ownsPendingOperandGroupingWarnings = savedPendingOperandGroupingWarnings == nullptr;
	if (ownsPendingOperandGroupingWarnings)
		context.pendingOperandGroupingWarnings = &localGroupingWarnings;
	bool savedDetectGroupingAmbiguity = context.detectGroupingAmbiguity;
	if (!context.trial)
		context.detectGroupingAmbiguity = true;
	struct GroupingContextRestore {
		InferenceContext &context;
		std::vector<InferenceContext::OperandGroupingWarning> *savedPendingOperandGroupingWarnings;
		bool savedDetectGroupingAmbiguity;
		~GroupingContextRestore() {
			context.pendingOperandGroupingWarnings = savedPendingOperandGroupingWarnings;
			context.detectGroupingAmbiguity = savedDetectGroupingAmbiguity;
		}
	} groupingContextRestore{context, savedPendingOperandGroupingWarnings, savedDetectGroupingAmbiguity};
	bool detectAmbiguity = context.detectGroupingAmbiguity;
	std::unordered_set<Expression *> selectedFixedGroupingRoots;
	GroupingSnapshot selectedGrouping;
	std::vector<InferenceContext::OperandGroupingWarning> selectedGroupingWarnings;
	std::string storedValidRendered;
	std::string ambiguousAlternativeRendered;
	bool foundValidGrouping = false;
	bool groupingAmbiguous = false;
	auto queueSelectedGroupingWarnings = [&]() {
		if (!context.pendingOperandGroupingWarnings || context.groupingAmbiguityIncomplete)
			return;
		context.pendingOperandGroupingWarnings->insert(
			context.pendingOperandGroupingWarnings->end(), selectedGroupingWarnings.begin(), selectedGroupingWarnings.end()
		);
	};
	auto queueCurrentGroupingWarning = [&](std::string chosenGrouping, std::string alternativeGrouping) {
		if (!context.pendingOperandGroupingWarnings || context.groupingAmbiguityIncomplete)
			return;
		std::vector<RelatedInfo> relatedInfo = context.captureInferenceTraceRelatedInfo(expr);
		for (const ParseContext::DeferredGroupingAmbiguity &deferred : context.parseContext.deferredGroupingAmbiguities) {
			if (expr && deferred.line == expr->range.line) {
				if (relatedInfo.empty()) {
					relatedInfo = deferred.relatedInfo;
					break;
				}
				auto storedTracePosition =
					std::find_if(deferred.relatedInfo.begin(), deferred.relatedInfo.end(), [&](const RelatedInfo &stored) {
					const Range &current = relatedInfo.back().range;
					return stored.range.line == current.line && stored.range.subString == current.subString;
				});
				requireCompilerInvariant(
					storedTracePosition != deferred.relatedInfo.end(),
					"stable grouping trace does not reconnect to its deferred inference trace"
				);
				relatedInfo.insert(relatedInfo.end(), storedTracePosition + 1, deferred.relatedInfo.end());
				break;
			}
		}
		context.pendingOperandGroupingWarnings->push_back({
			expr ? expr->range : Range(),
			originalDiagnostic.text,
			std::move(chosenGrouping),
			std::move(alternativeGrouping),
			std::move(relatedInfo),
		});
	};
	auto emitOwnedGroupingWarnings = [&]() {
		if (!ownsPendingOperandGroupingWarnings || context.trial || context.groupingAmbiguityIncomplete)
			return;
		for (const auto &warning : localGroupingWarnings) {
			std::string warningKey =
				buildOperandGroupingWarningKey(warning.range, warning.chosenGrouping, warning.alternativeGrouping);
			if (context.parseContext.emittedOperandGroupingWarnings.insert(warningKey).second) {
				Diagnostic diagnostic(
					context.parseContext, Diagnostic::Level::Warning, "ambiguous operand grouping", warning.range, "expression",
					warning.expressionText, "chosen", warning.chosenGrouping, "alternative", warning.alternativeGrouping
				);
				diagnostic.relatedInfo = warning.relatedInfo;
				context.addDiagnostic(std::move(diagnostic));
			}
		}
	};
	auto emitTypeFailureDiagnostic = [&]() {
		if (context.trial)
			return;
		if (context.hasTypeFailureDiagnostic) {
			context.addDiagnostic(context.typeFailureDiagnostic);
			return;
		}
		DiagnosticExpressionSnapshot failureSnapshot =
			context.typeFailureSnapshot.range.line ? context.typeFailureSnapshot : originalDiagnostic;
		context.addDiagnostic(buildTypeFailureDiagnostic(
			context.parseContext, failureSnapshot, context.typeFailureDetail, context.typeFailureRelatedInfo
		));
	};
	auto tryInfer = [&](bool collectGroupingAmbiguity = true) -> bool {
		context.clearTypeFailure();
		auto *savedFixedGroupingRoots = context.fixedGroupingRoots;
		auto *savedResolvedGroupingRoots = context.resolvedGroupingRoots;
		bool savedDetectGroupingAmbiguityDuringInfer = context.detectGroupingAmbiguity;
		if (!collectGroupingAmbiguity)
			context.detectGroupingAmbiguity = false;
		std::unordered_set<Expression *> commitResolvedGroupingRoots;
		context.fixedGroupingRoots = selectedFixedGroupingRoots.empty() ? nullptr : &selectedFixedGroupingRoots;
		context.resolvedGroupingRoots = &commitResolvedGroupingRoots;
		inferOrderedExpression(expr, context, flexBindingFrameStack, true);
		context.fixedGroupingRoots = savedFixedGroupingRoots;
		context.resolvedGroupingRoots = savedResolvedGroupingRoots;
		context.detectGroupingAmbiguity = savedDetectGroupingAmbiguityDuringInfer;
		if (context.typesValid && requireVoidResult) {
			DataType lineType = expr ? expr->type : DataType{};
			if (!lineType.isDeduced() || lineType.kind != DataType::Kind::Void) {
				context.fail(
					buildFailureDetailDiagnostic(
						originalDiagnostic.range, "Standalone expression '" + std::string(expr->range.subString) +
													  "' must return nothing; use discard if you want to ignore a value"
					),
					0
				);
				return false;
			}
		}
		if (context.typesValid && savedResolvedGroupingRoots)
			savedResolvedGroupingRoots->insert(selectedFixedGroupingRoots.begin(), selectedFixedGroupingRoots.end());
		return context.typesValid;
	};

	if (alreadyOrdered) {
		if (!tryInfer()) {
			context.typesValid = false;
			emitTypeFailureDiagnostic();
			return false;
		}
		emitOwnedGroupingWarnings();
		return true;
	}

	GroupingFailure trialFailure;
	if (!detectAmbiguity) {
		bool foundAcceptedGrouping = false;
		enumerateExpressionGroupings(
			expr, context, alreadyOrdered, flexBindingFrameStack,
			[&](Expression *&candidateExpr,
				const std::unordered_set<Expression *> &fixedGroupingRoots) -> GroupingEnumerationProgress {
			expr = candidateExpr;
			std::unordered_set<Expression *> resolvedGroupingRoots;
			GroupingSnapshot candidateGrouping;
			std::vector<InferenceContext::OperandGroupingWarning> candidateGroupingWarnings;
			bool accepted = validateGroupingInTrial(
				expr, context, fixedGroupingRoots, flexBindingFrameStack, originalDiagnostic, requireVoidResult, &trialFailure,
				&resolvedGroupingRoots, &candidateGrouping, &candidateGroupingWarnings
			);
			if (!accepted)
				return GroupingEnumerationProgress::EmittedContinue;
			if (accepted) {
				selectedGrouping = std::move(candidateGrouping);
				selectedFixedGroupingRoots = std::move(resolvedGroupingRoots);
				selectedGroupingWarnings = std::move(candidateGroupingWarnings);
				foundAcceptedGrouping = true;
			}
			return GroupingEnumerationProgress::Stop;
		}
		);

		if (foundAcceptedGrouping) {
			applyGroupingSnapshot(selectedGrouping);
			expr = selectedGrouping.root;
			recomputeRanges(expr);
			resetExpressionTypes(expr);
			if (!tryInfer(false)) {
				context.typesValid = false;
				if (!context.hasTypeFailureDiagnostic && trialFailure.hasDiagnostic)
					context.fail(trialFailure.diagnostic, trialFailure.priority);
				emitTypeFailureDiagnostic();
				return false;
			}
			queueSelectedGroupingWarnings();
			emitOwnedGroupingWarnings();
			return true;
		}

		applyGroupingSnapshot(initialGrouping);
		expr = initialGrouping.root;
		resetExpressionTypes(expr);
		context.typesValid = false;
		if (trialFailure.hasDiagnostic)
			context.fail(trialFailure.diagnostic, trialFailure.priority);
		emitTypeFailureDiagnostic();
		return false;
	}

	enumerateExpressionGroupings(
		expr, context, alreadyOrdered, flexBindingFrameStack,
		[&](Expression *&candidateExpr,
			const std::unordered_set<Expression *> &fixedGroupingRoots) -> GroupingEnumerationProgress {
		expr = candidateExpr;
		std::unordered_set<Expression *> resolvedGroupingRoots;
		GroupingSnapshot candidateGrouping;
		std::vector<InferenceContext::OperandGroupingWarning> candidateGroupingWarnings;
		bool accepted = validateGroupingInTrial(
			expr, context, fixedGroupingRoots, flexBindingFrameStack, originalDiagnostic, requireVoidResult, &trialFailure,
			&resolvedGroupingRoots, &candidateGrouping, &candidateGroupingWarnings
		);
		if (!accepted)
			return GroupingEnumerationProgress::EmittedContinue;

		applyGroupingSnapshot(candidateGrouping);
		expr = candidateGrouping.root;
		std::string candidateRendered = renderResolvedExpression(expr);
		applyGroupingSnapshot(initialGrouping);
		expr = initialGrouping.root;

		if (!foundValidGrouping) {
			selectedGrouping = std::move(candidateGrouping);
			selectedGroupingWarnings = std::move(candidateGroupingWarnings);
			storedValidRendered = candidateRendered;
			selectedFixedGroupingRoots = std::move(resolvedGroupingRoots);
			foundValidGrouping = true;
			return GroupingEnumerationProgress::EmittedContinue;
		}
		if (snapshotsHaveSameLocalOrdering(candidateGrouping, selectedGrouping))
			return GroupingEnumerationProgress::EmittedContinue;
		ambiguousAlternativeRendered = candidateRendered;
		groupingAmbiguous = true;
		return GroupingEnumerationProgress::Stop;
	}
	);

	if (foundValidGrouping) {
		applyGroupingSnapshot(selectedGrouping);
		expr = selectedGrouping.root;
		recomputeRanges(expr);
		resetExpressionTypes(expr);
		if (!tryInfer(false)) {
			context.typesValid = false;
			if (!context.hasTypeFailureDiagnostic && trialFailure.hasDiagnostic)
				context.fail(trialFailure.diagnostic, trialFailure.priority);
			emitTypeFailureDiagnostic();
			return false;
		}
		queueSelectedGroupingWarnings();
		if (groupingAmbiguous)
			queueCurrentGroupingWarning(storedValidRendered, ambiguousAlternativeRendered);
		emitOwnedGroupingWarnings();
		return true;
	}

	applyGroupingSnapshot(initialGrouping);
	expr = initialGrouping.root;
	resetExpressionTypes(expr);
	context.typesValid = false;
	if (trialFailure.hasDiagnostic)
		context.fail(trialFailure.diagnostic, trialFailure.priority);
	emitTypeFailureDiagnostic();
	return false;
}
