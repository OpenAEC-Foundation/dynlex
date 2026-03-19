#pragma once

#include "function_inference.inl"

static void recomputeRanges(Expression *expr) {
	if (!expr)
		return;
	for (Expression *arg : expr->arguments)
		recomputeRanges(arg);
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

static void resetExpressionTypes(Expression *expr) {
	if (!expr)
		return;
	if (expr->kind != Expression::Kind::Literal)
		expr->type = {};
	for (Expression *arg : expr->arguments)
		resetExpressionTypes(arg);
}

static void sortArgumentsRecursive(Expression *expr) {
	if (!expr)
		return;
	for (Expression *arg : expr->arguments)
		sortArgumentsRecursive(arg);
	if (expr->kind == Expression::Kind::PatternCall)
		expr->arguments = sortArgumentsByPosition(expr->arguments);
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
	const BindingFrameStack &macroBindingFrameStack, std::string *trialFailureDetail = nullptr,
	std::unordered_set<Expression *> *resolvedGroupingRoots = nullptr
) {
	resetExpressionTypes(expr);
	InferenceContext::TrialJournal journal;
	InferenceContext trialContext(context.parseContext, true);
	trialContext.currentInstantiation = context.currentInstantiation;
	trialContext.trialJournal = &journal;
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
	if ((!trialSucceeded || !trialContext.typesValid) && trialFailureDetail && trialFailureDetail->empty() &&
		!trialContext.typeFailureDetail.empty())
		*trialFailureDetail = trialContext.typeFailureDetail;
	if ((!trialSucceeded || !trialContext.typesValid) && context.typeFailureDetail.empty() &&
		!trialContext.typeFailureDetail.empty())
		context.typeFailureDetail = trialContext.typeFailureDetail;
	if (trialSucceeded && trialContext.typesValid && resolvedGroupingRoots)
		*resolvedGroupingRoots = std::move(trialFixedGroupingRoots);
	rollbackTrialJournal(journal);
	return trialSucceeded && trialContext.typesValid;
}

static bool enumerateExpressionGroupings(
	Expression *&expr, InferenceContext &context, bool alreadyOrdered, const BindingFrameStack &macroBindingFrameStack,
	const std::function<bool(Expression *&, const std::unordered_set<Expression *> &)> &onCandidate
) {
	static bool traceNestedMacroType = std::getenv("DYNLEX_TRACE_NESTED_MACRO_TYPE") != nullptr;
	std::unordered_set<Expression *> fixedGroupingRoots;
	auto withFixedRoot = [&](Expression *candidate, const std::function<bool()> &continuation) -> bool {
		bool shouldFix = candidate && candidate->kind == Expression::Kind::PatternCall && !candidate->arguments.empty();
		bool inserted = shouldFix && fixedGroupingRoots.insert(candidate).second;
		bool accepted = continuation();
		if (inserted)
			fixedGroupingRoots.erase(candidate);
		return accepted;
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

	recomputeRanges(expr);
	sortArgumentsRecursive(expr);

	if (alreadyOrdered)
		return withFixedRoot(expr, [&]() -> bool {
			return onCandidate(expr, fixedGroupingRoots);
		});

	std::function<bool(Expression *&, bool, bool, bool, const std::function<bool()> &)> enumerateOpaqueChoices;
	enumerateOpaqueChoices = [&](Expression *&current, bool isOnBoundary, bool isRoot, bool forceLocal,
								 const std::function<bool()> &continuation) -> bool {
		bool isPatternCall = current->kind == Expression::Kind::PatternCall && !current->arguments.empty();
		if (!isPatternCall)
			return continuation();

		if (isOpaqueGroupingNode(current, isOnBoundary, isRoot, forceLocal)) {
			Expression *savedCurrent = current;
			bool preserveExplicitGroup = current->isExplicitGroup;
			InferenceContext::TrialJournal journal;
			InferenceContext trialContext(context.parseContext, true);
			trialContext.currentInstantiation = context.currentInstantiation;
			trialContext.trialJournal = &journal;
			Expression *groupedCurrent = current;
			bool found = enumerateExpressionGroupings(
				groupedCurrent, trialContext, false, macroBindingFrameStack,
				[&](Expression *&groupedExpr, const std::unordered_set<Expression *> &childFixedGroupingRoots) -> bool {
				if (preserveExplicitGroup && groupedExpr)
					groupedExpr->isExplicitGroup = true;
				std::string childFailureDetail;
				bool childValid = validateGroupingInTrial(
					groupedExpr, trialContext, childFixedGroupingRoots, macroBindingFrameStack, &childFailureDetail
				);
				if (!childValid)
					return false;
				if (!nestedGroupingHasResolvedType(groupedExpr))
					return false;
				current = groupedExpr;
				std::vector<Expression *> insertedRoots;
				insertedRoots.reserve(childFixedGroupingRoots.size());
				for (Expression *root : childFixedGroupingRoots) {
					if (fixedGroupingRoots.insert(root).second)
						insertedRoots.push_back(root);
				}
				bool accepted = continuation();
				for (Expression *root : insertedRoots)
					fixedGroupingRoots.erase(root);
				if (!accepted)
					current = savedCurrent;
				return accepted;
			}
			);
			if (!found)
				current = savedCurrent;
			if (!found && context.typeFailureDetail.empty() && !trialContext.typeFailureDetail.empty())
				context.typeFailureDetail = trialContext.typeFailureDetail;
			rollbackTrialJournal(journal);
			return found;
		}

		bool hasLeftEdge = startsWithArgument(current);
		bool hasRightEdge = endsWithArgument(current);
		std::function<bool(size_t)> enumerateArguments = [&](size_t argumentIndex) -> bool {
			if (argumentIndex >= current->arguments.size())
				return continuation();
			bool isLeftBoundaryArgument = hasLeftEdge && argumentIndex == 0;
			bool isRightBoundaryArgument = hasRightEdge && argumentIndex + 1 == current->arguments.size();
			bool forceLocalArgument = argumentHasAdjacentSiblingSlot(current, argumentIndex);
			bool found = enumerateOpaqueChoices(
				current->arguments[argumentIndex], isLeftBoundaryArgument || isRightBoundaryArgument, false, forceLocalArgument,
				[&]() -> bool {
				return enumerateArguments(argumentIndex + 1);
			}
			);
			return found;
		};
		return enumerateArguments(0);
	};

	return enumerateOpaqueChoices(expr, true, true, false, [&]() -> bool {
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
			return withFixedRoot(expr, [&]() -> bool {
				return onCandidate(expr, fixedGroupingRoots);
			});
		if (operatorCount > 8) {
			if (context.typeFailureDetail.empty())
				context.typeFailureDetail = "too many ambiguous operand groupings";
			return false;
		}

		std::function<bool(int, int, std::function<bool(Expression *)>)> tryGroupings =
			[&](int start, int end, std::function<bool(Expression *)> onResult) -> bool {
			if (start > end)
				return false;
			if (start == end)
				return onResult(flatNodes[start]);

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
				auto tryRight = [&]() -> bool {
					if (!hasRightEdge)
						return onResult(rootExpression);
					return tryGroupings(rootIndex + 1, end, [&](Expression *rightResult) -> bool {
						if (expressionContains(rightResult, rootExpression))
							return false;
						rootExpression->arguments.back() = rightResult;
						return onResult(rootExpression);
					});
				};
				bool done = false;
				if (hasLeftEdge) {
					done = tryGroupings(start, rootIndex - 1, [&](Expression *leftResult) -> bool {
						if (expressionContains(leftResult, rootExpression))
							return false;
						rootExpression->arguments.front() = leftResult;
						return tryRight();
					});
				} else {
					done = tryRight();
				}
				if (done)
					return true;
				if (hasLeftEdge)
					rootExpression->arguments.front() = savedLeft;
				if (hasRightEdge)
					rootExpression->arguments.back() = savedRight;
			}
			return false;
		};

		int lastIndex = (int)flatNodes.size() - 1;
		return tryGroupings(0, lastIndex, [&](Expression *rootExpression) -> bool {
			expr = rootExpression;
			recomputeRanges(expr);
			sortArgumentsRecursive(expr);
			return withFixedRoot(expr, [&]() -> bool {
				return onCandidate(expr, fixedGroupingRoots);
			});
		});
	});
}

static bool inferExpression(
	Expression *&expr, InferenceContext &context, bool alreadyOrdered, const BindingFrameStack &macroBindingFrameStack
) {
	Expression *originalExpr = cloneExpressionTree(expr);
	std::unordered_set<Expression *> selectedFixedGroupingRoots;
	auto releaseOriginalExpr = [&]() {
		deleteExpressionTree(originalExpr);
		originalExpr = nullptr;
	};
	auto tryInfer = [&]() -> bool {
		context.typeFailureDetail.clear();
		auto *savedFixedGroupingRoots = context.fixedGroupingRoots;
		auto *savedResolvedGroupingRoots = context.resolvedGroupingRoots;
		std::unordered_set<Expression *> commitResolvedGroupingRoots;
		context.fixedGroupingRoots = selectedFixedGroupingRoots.empty() ? nullptr : &selectedFixedGroupingRoots;
		context.resolvedGroupingRoots = &commitResolvedGroupingRoots;
		inferOrderedExpression(expr, context, macroBindingFrameStack, true);
		context.fixedGroupingRoots = savedFixedGroupingRoots;
		context.resolvedGroupingRoots = savedResolvedGroupingRoots;
		if (context.typesValid && !context.trial && context.currentInstantiation)
			recordSelectedOverloadsForInstantiation(expr, context.currentInstantiation);
		if (context.typesValid)
			snapshotExpressionVariableReferences(expr, context);
		if (context.typesValid && savedResolvedGroupingRoots)
			savedResolvedGroupingRoots->insert(selectedFixedGroupingRoots.begin(), selectedFixedGroupingRoots.end());
		return context.typesValid;
	};

	if (alreadyOrdered) {
		if (!tryInfer()) {
			context.typesValid = false;
			if (!context.trial)
				context.addDiagnostic(buildTypeFailureDiagnostic(context.parseContext, originalExpr, context.typeFailureDetail)
				);
			releaseOriginalExpr();
			return false;
		}
		releaseOriginalExpr();
		return true;
	}

	std::string trialFailureDetail;
	bool found = enumerateExpressionGroupings(
		expr, context, alreadyOrdered, macroBindingFrameStack,
		[&](Expression *&candidateExpr, const std::unordered_set<Expression *> &fixedGroupingRoots) -> bool {
		expr = candidateExpr;
		std::unordered_set<Expression *> resolvedGroupingRoots;
		bool accepted = validateGroupingInTrial(
			expr, context, fixedGroupingRoots, macroBindingFrameStack, &trialFailureDetail, &resolvedGroupingRoots
		);
		if (accepted)
			selectedFixedGroupingRoots = std::move(resolvedGroupingRoots);
		return accepted;
	}
	);

	if (found) {
		recomputeRanges(expr);
		sortArgumentsRecursive(expr);
		resetExpressionTypes(expr);
		if (!tryInfer()) {
			context.typesValid = false;
			if (context.typeFailureDetail.empty())
				context.typeFailureDetail = trialFailureDetail;
			if (!context.trial)
				context.addDiagnostic(buildTypeFailureDiagnostic(context.parseContext, originalExpr, context.typeFailureDetail)
				);
			releaseOriginalExpr();
			return false;
		}
		releaseOriginalExpr();
		return true;
	}

	expr = originalExpr;
	resetExpressionTypes(expr);
	context.typesValid = false;
	if (context.typeFailureDetail.empty())
		context.typeFailureDetail = trialFailureDetail;
	if (!context.trial)
		context.addDiagnostic(buildTypeFailureDiagnostic(context.parseContext, originalExpr, trialFailureDetail));
	return false;
}
