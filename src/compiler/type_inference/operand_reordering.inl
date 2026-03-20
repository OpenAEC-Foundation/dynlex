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

static void sortArgumentsRecursive(Expression *expr, std::unordered_set<Expression *> &visited) {
	if (!expr)
		return;
	if (!visited.insert(expr).second)
		return;
	for (Expression *arg : expr->arguments)
		sortArgumentsRecursive(arg, visited);
	if (expr->kind == Expression::Kind::PatternCall)
		expr->arguments = sortArgumentsByPosition(expr->arguments);
}

static void sortArgumentsRecursive(Expression *expr) {
	std::unordered_set<Expression *> visited;
	sortArgumentsRecursive(expr, visited);
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
	auto isOpaqueAtCurrentLevel = [&](Expression *expression, const GroupingSnapshot &snapshot) -> bool {
		bool isPatternCall =
			expression && expression->kind == Expression::Kind::PatternCall && !snapshotArguments(snapshot, expression).empty();
		if (!isPatternCall)
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
	for (size_t argumentIndex = 0; argumentIndex < leftArguments.size(); argumentIndex++) {
		bool isLeftBoundaryArgument = hasLeftEdge && argumentIndex == 0;
		bool isRightBoundaryArgument = hasRightEdge && argumentIndex + 1 == leftArguments.size();
		bool childForceLocal = argumentHasAdjacentSiblingSlot(leftExpr, argumentIndex);
		if (!snapshotsHaveSameLocalOrdering(
				left, right, leftArguments[argumentIndex], rightArguments[argumentIndex],
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
	if (expr->kind == Expression::Kind::PatternCall && !expr->arguments.empty())
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
	return expression->patternMatch->nodesPassed.front()->type == PatternElement::Type::Variable;
}

static bool endsWithArgument(Expression *expression) {
	return expression->patternMatch->nodesPassed.back()->type == PatternElement::Type::Variable;
}

static bool argumentHasAdjacentSiblingSlot(Expression *expression, size_t argumentIndex) {
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
	if (!expression || expression->kind != Expression::Kind::PatternCall || !expression->patternMatch ||
		!expression->patternMatch->matchedEndNode || expression->patternMatch->matchedEndNode->matchingDefinitions.empty())
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
	trialContext.trialJournal = &journal;
	trialContext.trialInstantiationCache =
		context.trialInstantiationCache ? context.trialInstantiationCache : context.ensureTrialInstantiationCache();
	trialContext.detectGroupingAmbiguity = context.detectGroupingAmbiguity;
	std::vector<InferenceContext::OperandGroupingWarning> trialGroupingWarnings;
	trialContext.pendingOperandGroupingWarnings = &trialGroupingWarnings;
	std::unordered_set<Expression *> trialFixedGroupingRoots = fixedGroupingRoots;
	trialContext.fixedGroupingRoots = &trialFixedGroupingRoots;
	trialContext.resolvedGroupingRoots = &trialFixedGroupingRoots;
	bool trialSucceeded = true;
	if (trialContext.currentInstantiation) {
		trialSucceeded = runInstantiationReinferenceLoop(
			trialContext, *trialContext.currentInstantiation, nullptr, expr->range, (std::string)expr->range.subString,
			[&]() -> bool {
			resetExpressionTypes(expr);
			inferOrderedExpression(expr, trialContext, macroBindingFrameStack, true);
			return trialContext.typesValid;
		}
		);
	} else {
		inferOrderedExpression(expr, trialContext, macroBindingFrameStack, true);
		trialSucceeded = trialContext.typesValid;
	}
	if (trialSucceeded && trialContext.typesValid && requireVoidResult) {
		DataType lineType = resolveTypeThroughBindings(expr, macroBindingFrameStack);
		if (!lineType.isDeduced() || lineType.kind != DataType::Kind::Void) {
			std::string detail = "Standalone expression '" + std::string(expr->range.subString) +
								 "' must return nothing; use discard if you want to ignore a value";
			if (trialFailureDetail && trialFailureDetail->empty())
				*trialFailureDetail = detail;
			if (context.typeFailureDetail.empty())
				context.typeFailureDetail = detail;
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
	if ((!trialSucceeded || !trialContext.typesValid) && context.typeFailureDetail.empty() &&
		!trialContext.typeFailureDetail.empty())
		context.typeFailureDetail = trialContext.typeFailureDetail;
	if (trialSucceeded && trialContext.typesValid && resolvedGroupingRoots)
		*resolvedGroupingRoots = std::move(trialFixedGroupingRoots);
	rollbackTrialJournal(journal);
	applyGroupingSnapshot(originalGrouping);
	expr = originalGrouping.root;
	recomputeRanges(expr);
	sortArgumentsRecursive(expr);
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
		bool shouldFix = candidate && candidate->kind == Expression::Kind::PatternCall && !candidate->arguments.empty();
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
			argTypesForOverload.push_back(resolveTypeThroughBindings(arg, macroBindingFrameStack));
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
	sortArgumentsRecursive(expr);

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
		bool isPatternCall = current->kind == Expression::Kind::PatternCall && !current->arguments.empty();
		if (!isPatternCall)
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
			if (!result.foundValid && context.typeFailureDetail.empty() && !trialContext.typeFailureDetail.empty())
				context.typeFailureDetail = trialContext.typeFailureDetail;
			rollbackTrialJournal(journal);
			return result;
		}

		bool hasLeftEdge = startsWithArgument(current);
		bool hasRightEdge = endsWithArgument(current);
		Expression *parentExpression = current;
		std::function<GroupingEnumerationResult(size_t)> enumerateArguments = [&](size_t argumentIndex
																			  ) -> GroupingEnumerationResult {
			if (argumentIndex >= parentExpression->arguments.size())
				return continuation();
			bool isLeftBoundaryArgument = hasLeftEdge && argumentIndex == 0;
			bool isRightBoundaryArgument = hasRightEdge && argumentIndex + 1 == parentExpression->arguments.size();
			bool forceLocalArgument = argumentHasAdjacentSiblingSlot(parentExpression, argumentIndex);
			Expression *argumentExpr = parentExpression->arguments[argumentIndex];
			GroupingEnumerationResult result = enumerateOpaqueChoices(
				argumentExpr, isLeftBoundaryArgument || isRightBoundaryArgument, false, forceLocalArgument,
				[&]() -> GroupingEnumerationResult {
				parentExpression->arguments[argumentIndex] = argumentExpr;
				return enumerateArguments(argumentIndex + 1);
			}
			);
			parentExpression->arguments[argumentIndex] = argumentExpr;
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
			bool isPatternCall = expression->kind == Expression::Kind::PatternCall && !expression->arguments.empty();

			if (!isPatternCall) {
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

			for (size_t i = 0; i < expression->arguments.size(); i++) {
				bool isLeftBoundaryArgument = hasLeftEdge && i == 0;
				bool isRightBoundaryArgument = hasRightEdge && i + 1 == expression->arguments.size();
				if (isRightBoundaryArgument)
					continue;
				if (isLeftBoundaryArgument)
					collectFlatNodes(expression->arguments[i], true, false);
			}

			operatorCount++;
			flatNodes.push_back(expression);

			if (hasRightEdge)
				collectFlatNodes(expression->arguments.back(), true, false);
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
				bool isPatternCall =
					rootExpression->kind == Expression::Kind::PatternCall && !rootExpression->arguments.empty();
				if (!isPatternCall || opaqueNodes.contains(rootExpression))
					continue;
				bool hasLeftEdge = startsWithArgument(rootExpression);
				bool hasRightEdge = endsWithArgument(rootExpression);
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
						if (opaqueNodes.contains(otherExpression) || otherExpression->kind != Expression::Kind::PatternCall ||
							otherExpression->arguments.empty())
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

				Expression *savedLeft = hasLeftEdge ? rootExpression->arguments.front() : nullptr;
				Expression *savedRight = hasRightEdge ? rootExpression->arguments.back() : nullptr;
				auto tryRight = [&]() -> GroupingEnumerationResult {
					if (!hasRightEdge)
						return onResult(rootExpression);
					return tryGroupings(rootIndex + 1, end, [&](Expression *rightResult) -> GroupingEnumerationResult {
						if (expressionContains(rightResult, rootExpression))
							return {};
						rootExpression->arguments.back() = rightResult;
						return onResult(rootExpression);
					});
				};
				GroupingEnumerationResult candidateResult;
				if (hasLeftEdge) {
					candidateResult =
						tryGroupings(start, rootIndex - 1, [&](Expression *leftResult) -> GroupingEnumerationResult {
						if (expressionContains(leftResult, rootExpression))
							return {};
						rootExpression->arguments.front() = leftResult;
						return tryRight();
					});
				} else {
					candidateResult = tryRight();
				}
				if (hasLeftEdge)
					rootExpression->arguments.front() = savedLeft;
				if (hasRightEdge)
					rootExpression->arguments.back() = savedRight;
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
			sortArgumentsRecursive(expr);
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
		context.typeFailureDetail.clear();
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
			DataType lineType = resolveTypeThroughBindings(expr, macroBindingFrameStack);
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
				context.addDiagnostic(
					buildTypeFailureDiagnostic(context.parseContext, originalDiagnostic, context.typeFailureDetail)
				);
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
			sortArgumentsRecursive(expr);
			resetExpressionTypes(expr);
			if (!tryInfer(false)) {
				context.typesValid = false;
				if (context.typeFailureDetail.empty())
					context.typeFailureDetail = trialFailureDetail;
				if (!context.trial)
					context.addDiagnostic(
						buildTypeFailureDiagnostic(context.parseContext, originalDiagnostic, context.typeFailureDetail)
					);
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
			context.typeFailureDetail = trialFailureDetail;
		if (!context.trial)
			context.addDiagnostic(buildTypeFailureDiagnostic(context.parseContext, originalDiagnostic, trialFailureDetail));
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
		sortArgumentsRecursive(expr);
		resetExpressionTypes(expr);
		if (!tryInfer(false)) {
			context.typesValid = false;
			if (context.typeFailureDetail.empty())
				context.typeFailureDetail = trialFailureDetail;
			if (!context.trial)
				context.addDiagnostic(
					buildTypeFailureDiagnostic(context.parseContext, originalDiagnostic, context.typeFailureDetail)
				);
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
		context.typeFailureDetail = trialFailureDetail;
	if (!context.trial)
		context.addDiagnostic(buildTypeFailureDiagnostic(context.parseContext, originalDiagnostic, trialFailureDetail));
	return false;
}
