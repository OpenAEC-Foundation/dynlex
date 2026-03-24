#pragma once

#include "function_inference.inl"

static bool expressionTreeHasCycle(Expression *expr, std::unordered_set<Expression *> &visiting) {
	if (!expr)
		return false;
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

static void recomputeRanges(Expression *expr, std::unordered_set<Expression *> &visited) {
	if (!expr)
		return;
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
	if (!root)
		return false;
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
	if (!expr)
		return;
	if (!visited.insert(expr).second)
		return;
	if (expr->kind != Expression::Kind::Literal)
		expr->type = {};
	for (Expression *arg : expr->arguments)
		resetExpressionTypes(arg, visited);
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
	if (!expr)
		return "<null>";
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
		if (!node)
			continue;
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

struct GroupingSnapshot {
	Expression *root{};
	std::unordered_map<Expression *, std::vector<Expression *>> argumentsByExpression;
};

enum class GroupingCandidateDecision {
	Reject,
	AcceptContinue,
	AcceptStop,
};

struct GroupingEnumerationResult {
	bool foundValid{};
	bool stopRequested{};
};

static void captureGroupingSnapshot(Expression *expr, GroupingSnapshot &snapshot, std::unordered_set<Expression *> &visited) {
	if (!expr || !visited.insert(expr).second)
		return;
	snapshot.argumentsByExpression[expr] = expr->arguments;
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
}

static bool expressionHasGroupingShape(Expression *expression) {
	if (!expression)
		return false;
	if (expression->kind == Expression::Kind::PatternCall && !expression->arguments.empty())
		return true;
	return !expression->groupingArgumentIndices.empty();
}

static size_t groupingArgumentCount(Expression *expression) {
	if (!expression)
		return 0;
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

static void collectSelectedGroupingRoots(
	Expression *expr, std::unordered_set<Expression *> &roots, std::unordered_set<Expression *> &visited
) {
	if (!expr || !visited.insert(expr).second)
		return;
	if (expressionHasGroupingShape(expr))
		roots.insert(expr);
	for (Expression *arg : expr->arguments)
		collectSelectedGroupingRoots(arg, roots, visited);
}

static std::unordered_set<Expression *> collectSelectedGroupingRoots(Expression *expr) {
	std::unordered_set<Expression *> roots;
	std::unordered_set<Expression *> visited;
	collectSelectedGroupingRoots(expr, roots, visited);
	return roots;
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
	if (!expression)
		return false;
	if (expression->kind == Expression::Kind::PatternCall)
		return expression->patternMatch->nodesPassed.front()->type == PatternElement::Type::Variable;
	return expression->groupingStartsWithArgument;
}

static bool endsWithArgument(Expression *expression) {
	if (!expression)
		return false;
	if (expression->kind == Expression::Kind::PatternCall)
		return expression->patternMatch->nodesPassed.back()->type == PatternElement::Type::Variable;
	return expression->groupingEndsWithArgument;
}

static bool argumentHasAdjacentSiblingSlot(Expression *expression, size_t argumentIndex) {
	if (!expression)
		return false;
	if (expression->kind != Expression::Kind::PatternCall) {
		if (argumentIndex >= expression->groupingArgumentHasAdjacentSiblingSlot.size())
			return false;
		return expression->groupingArgumentHasAdjacentSiblingSlot[argumentIndex];
	}
	if (!expression || !expression->patternMatch || !expression->patternMatch->matchedEndNode)
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
	if (!expression)
		return 0;
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
	const BindingFrameStack &macroBindingFrameStack, bool requireVoidResult = false, std::string *trialFailureDetail = nullptr,
	std::unordered_set<Expression *> *resolvedGroupingRoots = nullptr, GroupingSnapshot *resolvedGroupingSnapshot = nullptr,
	std::vector<InferenceContext::OperandGroupingWarning> *resolvedGroupingWarnings = nullptr
) {
	GroupingSnapshot originalGrouping = captureGroupingSnapshot(expr);
	if (expressionTreeHasCycle(expr)) {
		if (trialFailureDetail && trialFailureDetail->empty())
			*trialFailureDetail = "internal operand regrouping cycle detected";
		if (context.typeFailureDetail.empty())
			context.typeFailureDetail = "internal operand regrouping cycle detected";
		return false;
	}
	resetExpressionTypes(expr);
	InferenceContext::TrialJournal journal;
	InferenceContext trialContext(context.parseContext, true);
	trialContext.currentInstantiation = context.currentInstantiation;
	trialContext.currentKnownConstants = context.currentKnownConstants;
	trialContext.trialJournal = &journal;
	trialContext.trialInstantiationCache =
		context.trialInstantiationCache ? context.trialInstantiationCache : context.ensureTrialInstantiationCache();
	trialContext.detectGroupingAmbiguity = context.detectGroupingAmbiguity;
	std::vector<InferenceContext::OperandGroupingWarning> trialGroupingWarnings;
	trialContext.pendingOperandGroupingWarnings = &trialGroupingWarnings;
	std::unordered_set<Expression *> trialFixedGroupingRoots = fixedGroupingRoots;
	trialContext.fixedGroupingRoots = &trialFixedGroupingRoots;
	trialContext.resolvedGroupingRoots = &trialFixedGroupingRoots;
	inferOrderedExpression(expr, trialContext, macroBindingFrameStack, true);
	bool trialSucceeded = trialContext.typesValid;
	bool trialDeferredToReinfer =
		trialSucceeded && trialContext.currentInstantiation && trialContext.currentInstantiation->needsReinfer;
	if (trialSucceeded && trialContext.typesValid && requireVoidResult) {
		Expression *lineExprForType = expr;
		DataType lineType = inferExpressionTypeWithoutSideEffects(lineExprForType, trialContext, macroBindingFrameStack);
		if (!trialDeferredToReinfer && (!lineType.isDeduced() || lineType.kind != DataType::Kind::Void)) {
			std::string detail = "Standalone expression '" + std::string(expr->range.subString) +
								 "' must return nothing; use discard if you want to ignore a value";
			if (trialFailureDetail && trialFailureDetail->empty())
				*trialFailureDetail = detail;
			if (trialContext.typeFailureDetail.empty())
				trialContext.typeFailureDetail = detail;
			trialSucceeded = false;
		}
	}
	if (trialSucceeded && trialContext.typesValid && resolvedGroupingSnapshot)
		*resolvedGroupingSnapshot = captureGroupingSnapshot(expr);
	if (trialSucceeded && trialContext.typesValid && resolvedGroupingWarnings)
		*resolvedGroupingWarnings = std::move(trialGroupingWarnings);
	if ((!trialSucceeded || !trialContext.typesValid) && trialFailureDetail && trialFailureDetail->empty() &&
		!trialContext.typeFailureDetail.empty())
		*trialFailureDetail = trialContext.typeFailureDetail;
	if (!trialSucceeded || !trialContext.typesValid)
		context.inheritTypeFailureFrom(trialContext);
	if (trialSucceeded && trialContext.typesValid && resolvedGroupingRoots)
		*resolvedGroupingRoots = std::move(trialFixedGroupingRoots);
	rollbackTrialJournal(journal);
	applyGroupingSnapshot(originalGrouping);
	expr = originalGrouping.root;
	recomputeRanges(expr);
	resetExpressionTypes(expr);
	return trialSucceeded && trialContext.typesValid;
}

static GroupingEnumerationResult enumerateExpressionGroupings(
	Expression *&expr, InferenceContext &context, bool alreadyOrdered, const BindingFrameStack &macroBindingFrameStack,
	const std::function<GroupingCandidateDecision(Expression *&, const std::unordered_set<Expression *> &)> &onCandidate
) {
	static bool traceNestedMacroType = std::getenv("DYNLEX_TRACE_NESTED_MACRO_TYPE") != nullptr;
	std::unordered_set<Expression *> fixedGroupingRoots;
	bool stopRequested = false;
	auto withFixedRoot = [&](Expression *candidate,
							 const std::function<GroupingEnumerationResult()> &continuation) -> GroupingEnumerationResult {
		bool shouldFix = expressionHasGroupingShape(candidate);
		bool inserted = shouldFix && fixedGroupingRoots.insert(candidate).second;
		GroupingEnumerationResult result = continuation();
		if (inserted)
			fixedGroupingRoots.erase(candidate);
		return result;
	};
	auto isOpaqueGroupingNode = [&](Expression *expression, bool isOnBoundary, bool isRoot, bool forceLocal) -> bool {
		return !isRoot && (forceLocal || expression->isExplicitGroup || !isOnBoundary);
	};
	auto isMacroPatternCall = [&](Expression *candidate) -> bool {
		if (!candidate || candidate->kind != Expression::Kind::PatternCall || !candidate->patternMatch ||
			!candidate->patternMatch->matchedEndNode)
			return false;
		auto &defs = candidate->patternMatch->matchedEndNode->matchingDefinitions;
		if (defs.empty())
			return false;
		std::vector<DataType> argTypesForOverload;
		for (Expression *arg : candidate->arguments)
			argTypesForOverload.push_back(inferExpressionTypeWithoutSideEffects(arg, context, macroBindingFrameStack));
		PatternDefinition *def =
			selectOverload(defs, candidate->arguments, candidate->patternMatch->nodesPassed, argTypesForOverload);
		return def && def->section && def->section->isMacro;
	};
	auto nestedGroupingHasResolvedType = [&](Expression *candidate) -> bool {
		DataType resolvedType = inferExpressionTypeWithoutSideEffects(candidate, context, macroBindingFrameStack);
		if (traceNestedMacroType && isMacroPatternCall(candidate) && !resolvedType.isDeduced()) {
			std::cerr << "[nested-macro-type] unresolved expr='" << std::string(candidate->range.subString)
					  << "' binding_depth=" << macroBindingFrameStack.depth() << "\n";
		}
		if (!resolvedType.isDeduced() && context.typeFailureDetail.empty()) {
			context.typeFailureDetail = "nested grouped expression '" + std::string(candidate->range.subString) +
										"' could not be resolved during operand regrouping";
		}
		return resolvedType.isDeduced();
	};

	if (expressionTreeHasCycle(expr)) {
		if (context.typeFailureDetail.empty())
			context.typeFailureDetail = "internal operand regrouping cycle detected";
		return {};
	}
	recomputeRanges(expr);

	if (alreadyOrdered)
		return withFixedRoot(expr, [&]() -> GroupingEnumerationResult {
			GroupingCandidateDecision decision = onCandidate(expr, fixedGroupingRoots);
			if (decision == GroupingCandidateDecision::AcceptStop)
				stopRequested = true;
			return {decision != GroupingCandidateDecision::Reject, decision == GroupingCandidateDecision::AcceptStop};
		});

	std::function<
		GroupingEnumerationResult(Expression *&, bool, bool, bool, const std::function<GroupingEnumerationResult()> &)>
		enumerateOpaqueChoices;
	enumerateOpaqueChoices = [&](Expression *&current, bool isOnBoundary, bool isRoot, bool forceLocal,
								 const std::function<GroupingEnumerationResult()> &continuation) -> GroupingEnumerationResult {
		if (!expressionHasGroupingShape(current))
			return continuation();

		if (isOpaqueGroupingNode(current, isOnBoundary, isRoot, forceLocal)) {
			InferenceContext::TrialJournal journal;
			InferenceContext trialContext(context.parseContext, true);
			trialContext.currentInstantiation = context.currentInstantiation;
			trialContext.trialJournal = &journal;
			trialContext.trialInstantiationCache =
				context.trialInstantiationCache ? context.trialInstantiationCache : context.ensureTrialInstantiationCache();
			Expression *savedCurrent = current;
			bool preserveExplicitGroup = current->isExplicitGroup;
			Expression *groupedCurrent = current;
			GroupingEnumerationResult result = enumerateExpressionGroupings(
				groupedCurrent, trialContext, false, macroBindingFrameStack,
				[&](Expression *&groupedExpr,
					const std::unordered_set<Expression *> &childFixedGroupingRoots) -> GroupingCandidateDecision {
				if (preserveExplicitGroup && groupedExpr)
					groupedExpr->isExplicitGroup = true;
				std::string childFailureDetail;
				bool childValid = validateGroupingInTrial(
					groupedExpr, trialContext, childFixedGroupingRoots, macroBindingFrameStack, false, &childFailureDetail
				);
				if (!childValid)
					return GroupingCandidateDecision::Reject;
				if (!nestedGroupingHasResolvedType(groupedExpr))
					return GroupingCandidateDecision::Reject;
				current = groupedExpr;
				std::vector<Expression *> insertedRoots;
				insertedRoots.reserve(childFixedGroupingRoots.size());
				for (Expression *root : childFixedGroupingRoots) {
					if (fixedGroupingRoots.insert(root).second)
						insertedRoots.push_back(root);
				}
				GroupingEnumerationResult continuationResult = continuation();
				for (Expression *root : insertedRoots)
					fixedGroupingRoots.erase(root);
				if (!stopRequested && !continuationResult.stopRequested)
					current = savedCurrent;
				if (!continuationResult.foundValid)
					return GroupingCandidateDecision::Reject;
				return continuationResult.stopRequested ? GroupingCandidateDecision::AcceptStop
														: GroupingCandidateDecision::AcceptContinue;
			}
			);
			if (!stopRequested && !result.stopRequested)
				current = savedCurrent;
			if (!result.foundValid)
				context.inheritTypeFailureFrom(trialContext);
			rollbackTrialJournal(journal);
			return result;
		}

		bool hasLeftEdge = startsWithArgument(current);
		bool hasRightEdge = endsWithArgument(current);
		size_t sourceArgumentCount = groupingArgumentCount(current);
		Expression *parentExpression = current;
		std::function<GroupingEnumerationResult(size_t)> enumerateArguments = [&](size_t sourceArgumentIndex
																			  ) -> GroupingEnumerationResult {
			if (sourceArgumentIndex >= sourceArgumentCount)
				return continuation();
			int actualArgumentIndex = groupingArgumentIndex(parentExpression, sourceArgumentIndex);
			if (actualArgumentIndex < 0)
				return enumerateArguments(sourceArgumentIndex + 1);
			bool isLeftBoundaryArgument = hasLeftEdge && sourceArgumentIndex == 0;
			bool isRightBoundaryArgument = hasRightEdge && sourceArgumentIndex + 1 == sourceArgumentCount;
			bool forceLocalArgument = argumentHasAdjacentSiblingSlot(parentExpression, sourceArgumentIndex);
			Expression *argumentExpr = parentExpression->arguments[actualArgumentIndex];
			GroupingEnumerationResult result = enumerateOpaqueChoices(
				argumentExpr, isLeftBoundaryArgument || isRightBoundaryArgument, false, forceLocalArgument,
				[&]() -> GroupingEnumerationResult {
				parentExpression->arguments[actualArgumentIndex] = argumentExpr;
				return enumerateArguments(sourceArgumentIndex + 1);
			}
			);
			parentExpression->arguments[actualArgumentIndex] = argumentExpr;
			return result;
		};
		return enumerateArguments(0);
	};

	return enumerateOpaqueChoices(expr, true, true, false, [&]() -> GroupingEnumerationResult {
		std::vector<Expression *> flatNodes;
		std::unordered_set<Expression *> opaqueNodes;
		size_t operatorCount = 0;
		std::function<void(Expression *, bool, bool)> collectFlatNodes = [&](Expression *expression, bool isOnBoundary,
																			 bool isRoot) {
			if (!expressionHasGroupingShape(expression)) {
				flatNodes.push_back(expression);
				return;
			}

			if (isOpaqueGroupingNode(expression, isOnBoundary, isRoot, false)) {
				opaqueNodes.insert(expression);
				flatNodes.push_back(expression);
				return;
			}

			bool hasLeftEdge = startsWithArgument(expression);
			bool hasRightEdge = endsWithArgument(expression);

			size_t sourceArgumentCount = groupingArgumentCount(expression);
			if (hasLeftEdge && sourceArgumentCount > 0) {
				int leftArgumentIndex = groupingArgumentIndex(expression, 0);
				if (leftArgumentIndex >= 0)
					collectFlatNodes(expression->arguments[leftArgumentIndex], true, false);
			}

			operatorCount++;
			flatNodes.push_back(expression);

			if (hasRightEdge && sourceArgumentCount > 0) {
				int rightArgumentIndex = groupingArgumentIndex(expression, sourceArgumentCount - 1);
				if (rightArgumentIndex >= 0)
					collectFlatNodes(expression->arguments[rightArgumentIndex], true, false);
			}
		};

		collectFlatNodes(expr, true, true);
		if (operatorCount <= 1)
			return withFixedRoot(expr, [&]() -> GroupingEnumerationResult {
				GroupingCandidateDecision decision = onCandidate(expr, fixedGroupingRoots);
				if (decision == GroupingCandidateDecision::AcceptStop)
					stopRequested = true;
				return {decision != GroupingCandidateDecision::Reject, decision == GroupingCandidateDecision::AcceptStop};
			});
		if (operatorCount > 8) {
			if (context.typeFailureDetail.empty())
				context.typeFailureDetail = "too many ambiguous operand groupings";
			return {};
		}

		std::function<GroupingEnumerationResult(int, int, std::function<GroupingEnumerationResult(Expression *)>)>
			tryGroupings = [&](int start, int end,
							   std::function<GroupingEnumerationResult(Expression *)> onResult) -> GroupingEnumerationResult {
			if (start > end)
				return {};
			if (start == end)
				return onResult(flatNodes[start]);

			GroupingEnumerationResult result;
			for (int rootIndex = end; rootIndex >= start; rootIndex--) {
				Expression *rootExpression = flatNodes[rootIndex];
				if (!expressionHasGroupingShape(rootExpression) || opaqueNodes.contains(rootExpression))
					continue;
				bool hasLeftEdge = startsWithArgument(rootExpression);
				bool hasRightEdge = endsWithArgument(rootExpression);
				size_t sourceArgumentCount = groupingArgumentCount(rootExpression);
				if ((hasLeftEdge || hasRightEdge) && sourceArgumentCount == 0)
					continue;
				if (hasLeftEdge && rootIndex == start)
					continue;
				if (hasRightEdge && rootIndex == end)
					continue;
				if (!hasLeftEdge && rootIndex > start)
					continue;
				if (!hasRightEdge && rootIndex < end)
					continue;
				int rootPrecedence = expressionPrecedence(rootExpression);
				if (rootPrecedence > 0 && hasLeftEdge && hasRightEdge) {
					bool lowerPrecedenceExists = false;
					for (int otherIndex = start; otherIndex <= end; otherIndex++) {
						if (otherIndex == rootIndex)
							continue;
						Expression *otherExpression = flatNodes[otherIndex];
						if (opaqueNodes.contains(otherExpression) || !expressionHasGroupingShape(otherExpression))
							continue;
						if (!startsWithArgument(otherExpression) || !endsWithArgument(otherExpression))
							continue;
						int otherPrecedence = expressionPrecedence(otherExpression);
						if (otherPrecedence > 0 && otherPrecedence < rootPrecedence) {
							lowerPrecedenceExists = true;
							break;
						}
					}
					if (lowerPrecedenceExists)
						continue;
				}

				int leftArgumentIndex = hasLeftEdge ? groupingArgumentIndex(rootExpression, 0) : -1;
				int rightArgumentIndex = hasRightEdge ? groupingArgumentIndex(rootExpression, sourceArgumentCount - 1) : -1;
				Expression *savedLeft = leftArgumentIndex >= 0 ? rootExpression->arguments[leftArgumentIndex] : nullptr;
				Expression *savedRight = rightArgumentIndex >= 0 ? rootExpression->arguments[rightArgumentIndex] : nullptr;
				auto tryRight = [&]() -> GroupingEnumerationResult {
					if (!hasRightEdge)
						return onResult(rootExpression);
					return tryGroupings(rootIndex + 1, end, [&](Expression *rightResult) -> GroupingEnumerationResult {
						if (expressionContains(rightResult, rootExpression))
							return {};
						rootExpression->arguments[rightArgumentIndex] = rightResult;
						return onResult(rootExpression);
					});
				};
				GroupingEnumerationResult candidateResult;
				if (hasLeftEdge) {
					candidateResult =
						tryGroupings(start, rootIndex - 1, [&](Expression *leftResult) -> GroupingEnumerationResult {
						if (expressionContains(leftResult, rootExpression))
							return {};
						rootExpression->arguments[leftArgumentIndex] = leftResult;
						return tryRight();
					});
				} else {
					candidateResult = tryRight();
				}
				if (leftArgumentIndex >= 0)
					rootExpression->arguments[leftArgumentIndex] = savedLeft;
				if (rightArgumentIndex >= 0)
					rootExpression->arguments[rightArgumentIndex] = savedRight;
				result.foundValid = result.foundValid || candidateResult.foundValid;
				if (candidateResult.stopRequested) {
					stopRequested = true;
					result.stopRequested = true;
					return result;
				}
			}
			return result;
		};

		int lastIndex = (int)flatNodes.size() - 1;
		return tryGroupings(0, lastIndex, [&](Expression *rootExpression) -> GroupingEnumerationResult {
			expr = rootExpression;
			recomputeRanges(expr);
			return withFixedRoot(expr, [&]() -> GroupingEnumerationResult {
				GroupingCandidateDecision decision = onCandidate(expr, fixedGroupingRoots);
				if (decision == GroupingCandidateDecision::AcceptStop)
					stopRequested = true;
				return {decision != GroupingCandidateDecision::Reject, decision == GroupingCandidateDecision::AcceptStop};
			});
		});
	});
}

static bool inferExpression(
	Expression *&expr, InferenceContext &context, bool alreadyOrdered, const BindingFrameStack &macroBindingFrameStack,
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
		if (!context.pendingOperandGroupingWarnings)
			return;
		context.pendingOperandGroupingWarnings->insert(
			context.pendingOperandGroupingWarnings->end(), selectedGroupingWarnings.begin(), selectedGroupingWarnings.end()
		);
	};
	auto queueCurrentGroupingWarning = [&](std::string chosenGrouping, std::string alternativeGrouping) {
		if (!context.pendingOperandGroupingWarnings)
			return;
		context.pendingOperandGroupingWarnings->push_back({
			expr ? expr->range : Range(),
			(std::string)originalDiagnostic.range.subString,
			std::move(chosenGrouping),
			std::move(alternativeGrouping),
		});
	};
	auto emitOwnedGroupingWarnings = [&]() {
		if (!ownsPendingOperandGroupingWarnings || context.trial)
			return;
		for (const auto &warning : localGroupingWarnings) {
			std::string warningKey =
				buildOperandGroupingWarningKey(warning.range, warning.chosenGrouping, warning.alternativeGrouping);
			if (context.parseContext.emittedOperandGroupingWarnings.insert(warningKey).second) {
				context.addDiagnostic(Diagnostic(
					context.parseContext, Diagnostic::Level::Warning, "ambiguous operand grouping", warning.range, "expression",
					warning.expressionText, "chosen", warning.chosenGrouping, "alternative", warning.alternativeGrouping
				));
			}
		}
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
		inferOrderedExpression(expr, context, macroBindingFrameStack, true);
		context.fixedGroupingRoots = savedFixedGroupingRoots;
		context.resolvedGroupingRoots = savedResolvedGroupingRoots;
		context.detectGroupingAmbiguity = savedDetectGroupingAmbiguityDuringInfer;
		if (context.typesValid && !context.trial && context.currentInstantiation)
			recordSelectedOverloadsForInstantiation(expr, context.currentInstantiation);
		if (context.typesValid)
			snapshotExpressionVariableReferences(expr, context);
		if (context.typesValid && requireVoidResult) {
			Expression *lineExprForType = expr;
			DataType lineType = inferExpressionTypeWithoutSideEffects(lineExprForType, context, macroBindingFrameStack);
			if (!lineType.isDeduced() || lineType.kind != DataType::Kind::Void) {
				context.typesValid = false;
				context.typeFailureDetail = "Standalone expression '" + std::string(expr->range.subString) +
											"' must return nothing; use discard if you want to ignore a value";
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
			if (!context.trial)
				context.addDiagnostic(buildTypeFailureDiagnostic(
					context.parseContext, originalDiagnostic, context.typeFailureDetail, context.typeFailureRelatedInfo
				));
			return false;
		}
		emitOwnedGroupingWarnings();
		return true;
	}

	std::string trialFailureDetail;
	if (!detectAmbiguity) {
		GroupingEnumerationResult groupingResult = enumerateExpressionGroupings(
			expr, context, alreadyOrdered, macroBindingFrameStack,
			[&](Expression *&candidateExpr,
				const std::unordered_set<Expression *> &fixedGroupingRoots) -> GroupingCandidateDecision {
			expr = candidateExpr;
			std::unordered_set<Expression *> resolvedGroupingRoots;
			GroupingSnapshot candidateGrouping;
			std::vector<InferenceContext::OperandGroupingWarning> candidateGroupingWarnings;
			bool accepted = validateGroupingInTrial(
				expr, context, fixedGroupingRoots, macroBindingFrameStack, requireVoidResult, &trialFailureDetail,
				&resolvedGroupingRoots, &candidateGrouping, &candidateGroupingWarnings
			);
			if (!accepted)
				return GroupingCandidateDecision::Reject;
			if (accepted) {
				selectedGrouping = std::move(candidateGrouping);
				selectedFixedGroupingRoots = std::move(resolvedGroupingRoots);
				selectedGroupingWarnings = std::move(candidateGroupingWarnings);
			}
			return GroupingCandidateDecision::AcceptStop;
		}
		);

		if (groupingResult.foundValid) {
			applyGroupingSnapshot(selectedGrouping);
			expr = selectedGrouping.root;
			selectedFixedGroupingRoots = collectSelectedGroupingRoots(expr);
			recomputeRanges(expr);
			resetExpressionTypes(expr);
			if (!tryInfer(false)) {
				context.typesValid = false;
				if (context.typeFailureDetail.empty())
					context.setTypeFailure(trialFailureDetail);
				if (!context.trial)
					context.addDiagnostic(buildTypeFailureDiagnostic(
						context.parseContext, originalDiagnostic, context.typeFailureDetail, context.typeFailureRelatedInfo
					));
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
		if (context.typeFailureDetail.empty())
			context.setTypeFailure(trialFailureDetail);
		if (!context.trial)
			context.addDiagnostic(buildTypeFailureDiagnostic(
				context.parseContext, originalDiagnostic, context.typeFailureDetail, context.typeFailureRelatedInfo
			));
		return false;
	}

	enumerateExpressionGroupings(
		expr, context, alreadyOrdered, macroBindingFrameStack,
		[&](Expression *&candidateExpr,
			const std::unordered_set<Expression *> &fixedGroupingRoots) -> GroupingCandidateDecision {
		expr = candidateExpr;
		std::unordered_set<Expression *> resolvedGroupingRoots;
		GroupingSnapshot candidateGrouping;
		std::vector<InferenceContext::OperandGroupingWarning> candidateGroupingWarnings;
		bool accepted = validateGroupingInTrial(
			expr, context, fixedGroupingRoots, macroBindingFrameStack, requireVoidResult, &trialFailureDetail,
			&resolvedGroupingRoots, &candidateGrouping, &candidateGroupingWarnings
		);
		if (!accepted)
			return GroupingCandidateDecision::Reject;

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
			return GroupingCandidateDecision::AcceptContinue;
		}
		if (snapshotsHaveSameLocalOrdering(candidateGrouping, selectedGrouping))
			return GroupingCandidateDecision::AcceptContinue;
		ambiguousAlternativeRendered = candidateRendered;
		groupingAmbiguous = true;
		return GroupingCandidateDecision::AcceptStop;
	}
	);

	if (foundValidGrouping) {
		applyGroupingSnapshot(selectedGrouping);
		expr = selectedGrouping.root;
		selectedFixedGroupingRoots = collectSelectedGroupingRoots(expr);
		recomputeRanges(expr);
		resetExpressionTypes(expr);
		if (!tryInfer(false)) {
			context.typesValid = false;
			if (context.typeFailureDetail.empty())
				context.setTypeFailure(trialFailureDetail);
			if (!context.trial)
				context.addDiagnostic(buildTypeFailureDiagnostic(
					context.parseContext, originalDiagnostic, context.typeFailureDetail, context.typeFailureRelatedInfo
				));
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
	if (context.typeFailureDetail.empty())
		context.setTypeFailure(trialFailureDetail);
	if (!context.trial)
		context.addDiagnostic(buildTypeFailureDiagnostic(
			context.parseContext, originalDiagnostic, context.typeFailureDetail, context.typeFailureRelatedInfo
		));
	return false;
}
