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
static SuspendedGroupingLayout captureSuspendedGroupingLayout(Expression *root, const ExpressionNodeSet &expressionNodes) {
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
	ExpressionNodeSet originalExpressionNodes;
	std::vector<Expression *> flatNodes;
	ExpressionNodeSet opaqueNodes;
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
canClaimGroupingSpan(int start, int end, int rootIndex, Expression *rootExpression, const GroupingGenerationState &state) {
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
	return true;
}

static bool
isEligibleGroupingRoot(int start, int end, int rootIndex, Expression *rootExpression, GroupingGenerationState &state) {
	if (!canClaimGroupingSpan(start, end, rootIndex, rootExpression, state))
		return false;
	for (int otherIndex = start; otherIndex <= end; otherIndex++) {
		if (otherIndex == rootIndex)
			continue;
		Expression *otherExpression = state.flatNodes[otherIndex];
		if (!canClaimGroupingSpan(start, end, otherIndex, otherExpression, state))
			continue;
		if (expressionMustEvaluateBefore(rootExpression, otherExpression))
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
	GroupingGenerationState state{context, flexBindingFrameStack, {}, {}, {}, {}};
	if (collectExpressionNodes(expr, state.originalExpressionNodes)) {
		if (!context.hasTypeFailureDiagnostic)
			context.fail(
				buildFailureDetailDiagnostic(
					captureDiagnosticExpressionSnapshot(expr).range, "internal operand regrouping cycle detected"
				),
				1
			);
		co_return;
	}

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
		ExpressionNodeSet candidateExpressionNodes;
		bool candidateHasCycle = collectExpressionNodes(expr, candidateExpressionNodes);
		requireCompilerInvariant(
			!candidateHasCycle && expressionNodeSetsEqual(candidateExpressionNodes, state.originalExpressionNodes),
			"operand regrouping changed the expression node set"
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
