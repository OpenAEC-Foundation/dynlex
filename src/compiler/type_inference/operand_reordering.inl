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

// Operand regrouping only cares whether the matched pattern starts with an
// argument slot and/or ends with an argument slot. For example, "$ $ + $" has
// exactly one left boundary slot and one right boundary slot: the middle "$"
// is interior even though it is also a variable element. There can never be
// multiple left or right boundary slots.
static bool startsWithArgument(Expression *expression) {
	return expression->patternMatch->nodesPassed.front()->type == PatternElement::Type::Variable;
}

static bool endsWithArgument(Expression *expression) {
	return expression->patternMatch->nodesPassed.back()->type == PatternElement::Type::Variable;
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

static bool absorbOperatorIntoBoundaryArgument(Expression *&expr, bool allowExplicitGroupRoot = false) {
	if (!expr)
		return false;

	bool changed = false;
	for (Expression *&arg : expr->arguments)
		changed = absorbOperatorIntoBoundaryArgument(arg, false) || changed;

	if (expr->kind != Expression::Kind::PatternCall || expr->arguments.empty() ||
		(expr->isExplicitGroup && !allowExplicitGroupRoot))
		return changed;

	bool hasLeftEdge = startsWithArgument(expr);
	bool hasRightEdge = endsWithArgument(expr);
	if (!hasLeftEdge && !hasRightEdge)
		return changed;

	return changed;
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

static bool inferExpression(
	Expression *&expr, InferenceContext &context, bool alreadyOrdered,
	const BindingFrameStack &macroBindingFrameStack = BindingFrameStack{}
) {
	static bool traceNestedMacroType = std::getenv("DYNLEX_TRACE_NESTED_MACRO_TYPE") != nullptr;
	recomputeRanges(expr);
	sortArgumentsRecursive(expr);
	absorbOperatorIntoBoundaryArgument(expr, true);
	recomputeRanges(expr);
	sortArgumentsRecursive(expr);
	Expression *originalExpr = cloneExpressionTree(expr);
	auto releaseOriginalExpr = [&]() {
		deleteExpressionTree(originalExpr);
		originalExpr = nullptr;
	};
	auto inferNestedForGrouping = [&](Expression *subExpr, Expression *&updatedSubExpr) -> bool {
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

		InferenceContext::TrialJournal journal;
		InferenceContext trialContext(context.parseContext, true);
		trialContext.currentInstantiation = context.currentInstantiation;
		trialContext.trialJournal = &journal;
		Expression *inferredExpr = subExpr;
		bool ok = inferExpression(inferredExpr, trialContext, false, macroBindingFrameStack);
		if (!ok && context.typeFailureDetail.empty())
			context.typeFailureDetail = trialContext.typeFailureDetail;
		if (ok && isMacroPatternCall(inferredExpr)) {
			DataType resolvedType = inferExpressionTypeWithoutSideEffects(inferredExpr, context, macroBindingFrameStack);
			if (traceNestedMacroType && !resolvedType.isDeduced()) {
				std::cerr << "[nested-macro-type] unresolved expr='" << std::string(inferredExpr->range.subString)
						  << "' binding_depth=" << macroBindingFrameStack.depth() << "\n";
			}
			ok = resolvedType.isDeduced();
		}
		updatedSubExpr = inferredExpr;
		rollbackTrialJournal(journal);
		return ok;
	};

	auto tryInfer = [&]() -> bool {
		context.typeFailureDetail.clear();
		inferOrderedExpression(expr, context, macroBindingFrameStack);
		if (context.typesValid && !context.trial && context.currentInstantiation)
			recordSelectedOverloadsForInstantiation(expr, context.currentInstantiation);
		if (context.typesValid)
			snapshotExpressionVariableReferences(expr, context);
		return context.typesValid;
	};

	if (alreadyOrdered) {
		if (!tryInfer()) {
			context.addDiagnostic(buildTypeFailureDiagnostic(context.parseContext, originalExpr, context.typeFailureDetail));
			releaseOriginalExpr();
			return false;
		}
		releaseOriginalExpr();
		return true;
	}
	std::vector<Expression *> flatNodes;
	size_t operatorCount = 0;
	std::function<void(Expression *, bool, bool)> collectFlatNodes = [&](Expression *expression, bool isOnBoundary,
																		 bool isRoot) {
		bool isPatternCall = expression->kind == Expression::Kind::PatternCall && !expression->arguments.empty();

		if (!isPatternCall) {
			flatNodes.push_back(expression);
			return;
		}

		if (!isRoot && (expression->isExplicitGroup || !isOnBoundary || !expression->isSubMatch)) {
			Expression *updatedExpression = expression;
			if (!inferNestedForGrouping(expression, updatedExpression)) {
				context.typesValid = false;
				return;
			}
			expression = updatedExpression;
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
			Expression *argument = expression->arguments[i];
			if (isLeftBoundaryArgument) {
				collectFlatNodes(argument, true, false);
				expression->arguments[i] = argument;
				if (!context.typesValid)
					return;
			} else {
				Expression *updatedArgument = argument;
				if (!inferNestedForGrouping(argument, updatedArgument)) {
					context.typesValid = false;
					return;
				}
				expression->arguments[i] = updatedArgument;
			}
		}

		operatorCount++;
		flatNodes.push_back(expression);

		if (hasRightEdge) {
			Expression *right = expression->arguments.back();
			collectFlatNodes(right, true, false);
			expression->arguments.back() = right;
		}
	};

	collectFlatNodes(expr, true, true);
	if (!context.typesValid) {
		expr = originalExpr;
		resetExpressionTypes(expr);
		context.addDiagnostic(buildTypeFailureDiagnostic(context.parseContext, originalExpr, context.typeFailureDetail));
		return false;
	}
	if (operatorCount <= 1) {
		if (!tryInfer()) {
			context.addDiagnostic(buildTypeFailureDiagnostic(context.parseContext, originalExpr, context.typeFailureDetail));
			releaseOriginalExpr();
			return false;
		}
		releaseOriginalExpr();
		return true;
	}

	size_t ambiguousOperatorCount = operatorCount;
	if (ambiguousOperatorCount > 8) {
		context.addDiagnostic(
			Diagnostic(context.parseContext, Diagnostic::Level::Error, "too many ambiguous operand groupings", expr->range)
		);
		releaseOriginalExpr();
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

			bool isPatternCall = rootExpression->kind == Expression::Kind::PatternCall && !rootExpression->arguments.empty();
			if (!isPatternCall)
				continue;
			bool hasLeftEdge = startsWithArgument(rootExpression);
			bool hasRightEdge = endsWithArgument(rootExpression);
			//
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
					if (otherExpression->kind != Expression::Kind::PatternCall || otherExpression->arguments.empty())
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
			auto tryRight = [&](void) -> bool {
				if (!hasRightEdge)
					return onResult(rootExpression);
				return tryGroupings(rootIndex + 1, end, [&](Expression *rightResult) -> bool {
					rootExpression->arguments.back() = rightResult;
					return onResult(rootExpression);
				});
			};
			bool done = false;
			if (hasLeftEdge) {
				done = tryGroupings(start, rootIndex - 1, [&](Expression *leftResult) -> bool {
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
	std::string trialFailureDetail;
	bool found = tryGroupings(0, lastIndex, [&](Expression *rootExpression) -> bool {
		expr = rootExpression;
		absorbOperatorIntoBoundaryArgument(expr);
		recomputeRanges(expr);
		sortArgumentsRecursive(expr);
		resetExpressionTypes(expr);
		InferenceContext::TrialJournal journal;
		InferenceContext trialContext(context.parseContext, true);
		trialContext.currentInstantiation = context.currentInstantiation;
		trialContext.trialJournal = &journal;
		bool trialSucceeded = true;
		if (trialContext.currentInstantiation) {
			trialSucceeded = runInstantiationReinferenceLoop(
				trialContext, *trialContext.currentInstantiation, nullptr, expr->range, (std::string)expr->range.subString,
				[&]() -> bool {
					resetExpressionTypes(expr);
					inferOrderedExpression(expr, trialContext, macroBindingFrameStack);
					return trialContext.typesValid;
				}
			);
		} else {
			inferOrderedExpression(expr, trialContext, macroBindingFrameStack);
			trialSucceeded = trialContext.typesValid;
		}
		if ((!trialSucceeded || !trialContext.typesValid) && trialFailureDetail.empty() &&
			!trialContext.typeFailureDetail.empty())
			trialFailureDetail = trialContext.typeFailureDetail;
		rollbackTrialJournal(journal);
		return trialSucceeded && trialContext.typesValid;
	});

	if (found) {
		recomputeRanges(expr);
		sortArgumentsRecursive(expr);
		resetExpressionTypes(expr);
		(void)tryInfer();
		releaseOriginalExpr();
		return context.typesValid;
	}

	expr = originalExpr;
	resetExpressionTypes(expr);
	context.addDiagnostic(buildTypeFailureDiagnostic(context.parseContext, originalExpr, trialFailureDetail));
	return false;
}
