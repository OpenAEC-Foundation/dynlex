#pragma once

#include "function_inference.inl"

static void recomputeRanges(Function *expr) {
	if (!expr)
		return;
	for (Function *arg : expr->arguments)
		recomputeRanges(arg);
	if (expr->kind == Function::Kind::PatternCall && !expr->arguments.empty()) {
		int originalStart = expr->range.start();
		int originalEnd = expr->range.end();
		int minStart = expr->arguments.front()->range.start();
		int maxEnd = expr->arguments.front()->range.end();
		for (Function *arg : expr->arguments) {
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

static void resetFunctionTypes(Function *expr) {
	if (!expr)
		return;
	if (expr->kind != Function::Kind::Literal)
		expr->type = {};
	for (Function *arg : expr->arguments)
		resetFunctionTypes(arg);
}

static void sortArgumentsRecursive(Function *expr) {
	if (!expr)
		return;
	for (Function *arg : expr->arguments)
		sortArgumentsRecursive(arg);
	if (expr->kind == Function::Kind::PatternCall)
		expr->arguments = sortArgumentsByPosition(expr->arguments);
}

static bool startsWithArgument(Function *function) {
	return function->patternMatch->nodesPassed.front()->type == PatternElement::Type::Variable;
}

static bool endsWithArgument(Function *function) {
	return function->patternMatch->nodesPassed.back()->type == PatternElement::Type::Variable;
}

static size_t countLeadingBoundaryArguments(Function *function);
static size_t countTrailingBoundaryArguments(Function *function);

static bool absorbOperatorIntoBoundaryArgument(Function *&expr) {
	if (!expr)
		return false;

	bool changed = false;
	for (Function *&arg : expr->arguments)
		changed = absorbOperatorIntoBoundaryArgument(arg) || changed;

	if (expr->kind != Function::Kind::PatternCall || expr->arguments.empty() || expr->isExplicitGroup)
		return changed;

	bool hasLeftEdge = startsWithArgument(expr);
	bool hasRightEdge = endsWithArgument(expr);
	if (!hasLeftEdge && !hasRightEdge)
		return changed;

	auto isPatternCallWithBoundary = [](Function *candidate) {
		return candidate && candidate->kind == Function::Kind::PatternCall && !candidate->arguments.empty() &&
			   !candidate->isExplicitGroup;
	};

	bool localChange = true;
	while (localChange) {
		localChange = false;

		if (!hasLeftEdge && hasRightEdge) {
			Function *right = expr->arguments.back();
			if (isPatternCallWithBoundary(right) && countLeadingBoundaryArguments(right) > 1) {
				Function *inner = cloneFunctionTree(expr);
				inner->arguments.back() = right->arguments.front();
				right->arguments.front() = inner;
				expr = right;
				changed = true;
				localChange = true;
			}
		}

		if (!localChange && hasLeftEdge && !hasRightEdge) {
			Function *left = expr->arguments.front();
			if (isPatternCallWithBoundary(left) && countTrailingBoundaryArguments(left) > 1) {
				Function *inner = cloneFunctionTree(expr);
				inner->arguments.front() = left->arguments.back();
				left->arguments.back() = inner;
				expr = left;
				changed = true;
				localChange = true;
			}
		}

		if (!localChange && hasLeftEdge && hasRightEdge) {
			Function *left = expr->arguments.front();
			Function *right = expr->arguments.back();

			if (isPatternCallWithBoundary(left) && countTrailingBoundaryArguments(left) > 1) {
				Function *inner = cloneFunctionTree(expr);
				inner->arguments.front() = left->arguments.back();
				inner->arguments.back() = right;
				left->arguments.back() = inner;
				expr = left;
				changed = true;
				localChange = true;
			} else if (isPatternCallWithBoundary(right) && countLeadingBoundaryArguments(right) > 1) {
				Function *inner = cloneFunctionTree(expr);
				inner->arguments.front() = left;
				inner->arguments.back() = right->arguments.front();
				right->arguments.front() = inner;
				expr = right;
				changed = true;
				localChange = true;
			}
		}
	}

	return changed;
}

static size_t countBoundaryArguments(Function *function, bool fromStart) {
	if (!function || function->kind != Function::Kind::PatternCall || !function->patternMatch ||
		!function->patternMatch->matchedEndNode)
		return 0;

	size_t maxCount = 0;
	for (PatternDefinition *def : function->patternMatch->matchedEndNode->matchingDefinitions) {
		if (!def)
			continue;
		size_t count = 0;
		if (fromStart) {
			for (const PatternElement &elem : def->patternElements) {
				if (elem.type != PatternElement::Type::Variable)
					break;
				count++;
			}
		} else {
			for (auto it = def->patternElements.rbegin(); it != def->patternElements.rend(); ++it) {
				if (it->type != PatternElement::Type::Variable)
					break;
				count++;
			}
		}
		maxCount = std::max(maxCount, count);
	}
	return maxCount;
}

static size_t countLeadingBoundaryArguments(Function *function) { return countBoundaryArguments(function, true); }

static size_t countTrailingBoundaryArguments(Function *function) { return countBoundaryArguments(function, false); }

static bool hasMultipleBoundaryArguments(Function *function) {
	return countLeadingBoundaryArguments(function) > 1 || countTrailingBoundaryArguments(function) > 1;
}

static bool functionContainsExplicitReturn(Function *function) {
	if (!function)
		return false;
	if (function->kind == Function::Kind::IntrinsicCall && intrinsicKind(function->intrinsicName) == IntrinsicKind::Return)
		return true;
	std::unordered_map<std::string, Function *> ignoredBindings;
	Function *bodyExpr = expandMacroPatternCall(function, ignoredBindings);
	if (bodyExpr && functionContainsExplicitReturn(bodyExpr))
		return true;
	for (Function *arg : function->arguments) {
		if (functionContainsExplicitReturn(arg))
			return true;
	}
	return false;
}

static bool expandsToSelectIntrinsic(Function *function) {
	std::unordered_map<std::string, Function *> ignoredBindings;
	Function *bodyExpr = expandMacroPatternCall(function, ignoredBindings);
	return bodyExpr && bodyExpr->kind == Function::Kind::IntrinsicCall &&
		   intrinsicKind(bodyExpr->intrinsicName) == IntrinsicKind::Select;
}

static bool sectionDefaultsToVoid(Section *section) {
	if (!section || section->type != SectionType::Function)
		return false;
	for (Section *child : section->children) {
		for (CodeLine *line : child->codeLines) {
			if (functionContainsExplicitReturn(line->function))
				return false;
		}
	}
	return true;
}

static bool mustOwnEntireRange(Function *function) {
	if (!function || function->kind != Function::Kind::PatternCall || !function->patternMatch ||
		!function->patternMatch->matchedEndNode)
		return false;

	bool sawCandidate = false;
	for (PatternDefinition *def : function->patternMatch->matchedEndNode->matchingDefinitions) {
		if (!def || !def->section)
			continue;
		sawCandidate = true;
		if (def->section->isMacro) {
			std::unordered_map<std::string, Function *> ignoredBindings;
			Function *bodyExpr = expandMacroPatternCall(function, ignoredBindings);
			if (!bodyExpr)
				return false;
			if (bodyExpr->kind == Function::Kind::IntrinsicCall) {
				if (intrinsicKind(bodyExpr->intrinsicName) == IntrinsicKind::Return)
					continue;
				const IntrinsicInfo *info = findIntrinsic(bodyExpr->intrinsicName);
				if (info && info->returnKind == IntrinsicReturnKind::Void)
					continue;
			}
			return false;
		}
		if (!sectionDefaultsToVoid(def->section))
			return false;
	}
	if (!sawCandidate)
		return false;
	return true;
}

static int functionPrecedence(Function *function) {
	if (!function || function->isExplicitGroup || function->kind != Function::Kind::PatternCall || !function->patternMatch ||
		!function->patternMatch->matchedEndNode || function->patternMatch->matchedEndNode->matchingDefinitions.empty())
		return 0;
	auto countParameters = [](PatternDefinition *def) {
		if (!def)
			return 0;
		int count = 0;
		for (const auto &elem : def->patternElements) {
			if (elem.type == PatternElement::Type::Variable)
				count++;
		}
		return count;
	};
	int precedence = 0;
	for (PatternDefinition *def : function->patternMatch->matchedEndNode->matchingDefinitions) {
		if (!def || countParameters(def) != (int)function->arguments.size())
			continue;
		if (def->precedence <= 0)
			continue;
		if (precedence == 0 || def->precedence < precedence)
			precedence = def->precedence;
	}
	return precedence;
}

static bool inferFunction(
	Function *&expr, InferenceContext &context, bool alreadyOrdered,
	const std::unordered_map<std::string, Function *> &macroBindings = {}
) {
	static bool traceNestedMacroType = std::getenv("DYNLEX_TRACE_NESTED_MACRO_TYPE") != nullptr;
	recomputeRanges(expr);
	sortArgumentsRecursive(expr);
	absorbOperatorIntoBoundaryArgument(expr);
	recomputeRanges(expr);
	sortArgumentsRecursive(expr);
	Function *originalExpr = cloneFunctionTree(expr);
	auto releaseOriginalExpr = [&]() {
		deleteFunctionTree(originalExpr);
		originalExpr = nullptr;
	};
	auto inferNestedForGrouping = [&](Function *subExpr, Function *&updatedSubExpr) -> bool {
		auto isMacroPatternCall = [&](Function *candidate) -> bool {
			if (!candidate || candidate->kind != Function::Kind::PatternCall || !candidate->patternMatch ||
				!candidate->patternMatch->matchedEndNode)
				return false;
			auto &defs = candidate->patternMatch->matchedEndNode->matchingDefinitions;
			if (defs.empty())
				return false;
			std::vector<DataType> argTypesForOverload;
			for (Function *arg : candidate->arguments)
				argTypesForOverload.push_back(resolveTypeThroughBindings(arg, macroBindings));
			PatternDefinition *def =
				selectOverload(defs, candidate->arguments, candidate->patternMatch->nodesPassed, argTypesForOverload);
			return def && def->section && def->section->isMacro;
		};

		InferenceContext::TrialJournal journal;
		InferenceContext trialContext(context.parseContext, true);
		trialContext.currentInstantiation = context.currentInstantiation;
		trialContext.trialJournal = &journal;
		Function *inferredExpr = subExpr;
		bool ok = inferFunction(inferredExpr, trialContext, false, macroBindings);
		if (!ok && context.typeFailureDetail.empty()) {
			context.typeFailureDetail = trialContext.typeFailureDetail;
			if (context.typeFailureDetail.empty())
				context.typeFailureDetail = "Failed to infer nested argument '" + (std::string)subExpr->range.subString + "'";
		}
		if (ok && isMacroPatternCall(inferredExpr)) {
			DataType resolvedType = inferFunctionTypeWithoutSideEffects(inferredExpr, context, macroBindings);
			if (traceNestedMacroType && !resolvedType.isDeduced()) {
				std::cerr << "[nested-macro-type] unresolved expr='" << std::string(inferredExpr->range.subString)
						  << "' bindings={";
				bool first = true;
				for (const auto &[name, value] : macroBindings) {
					if (!first)
						std::cerr << ", ";
					first = false;
					std::cerr << name << "='" << (value ? std::string(value->range.subString) : "<null>") << "'";
				}
				std::cerr << "}\n";
			}
			ok = resolvedType.isDeduced();
			if (!ok && context.typeFailureDetail.empty())
				context.typeFailureDetail =
					"Unable to resolve macro result type for '" + (std::string)inferredExpr->range.subString + "'";
		}
		updatedSubExpr = inferredExpr;
		rollbackTrialJournal(journal);
		return ok;
	};

	auto tryInfer = [&]() -> bool {
		context.typeFailureDetail.clear();
		inferOrderedFunction(expr, context, macroBindings);
		if (context.typesValid && !context.trial && context.currentInstantiation)
			recordSelectedOverloadsForInstantiation(expr, context.currentInstantiation);
		if (context.typesValid)
			snapshotFunctionVariableReferences(expr, context);
		return context.typesValid;
	};

	if (alreadyOrdered) {
		if (!tryInfer()) {
			context.addDiagnostic(
				{Diagnostic::Level::Error, buildTypeFailureDiagnostic(originalExpr, context.typeFailureDetail),
				 originalExpr->range}
			);
			releaseOriginalExpr();
			return false;
		}
		releaseOriginalExpr();
		return true;
	}
	if (expandsToSelectIntrinsic(expr)) {
		if (!tryInfer()) {
			context.addDiagnostic(
				{Diagnostic::Level::Error, buildTypeFailureDiagnostic(originalExpr, context.typeFailureDetail),
				 originalExpr->range}
			);
			releaseOriginalExpr();
			return false;
		}
		releaseOriginalExpr();
		return true;
	}
	if (mustOwnEntireRange(expr)) {
		for (size_t i = 0; i < expr->arguments.size(); i++) {
			// Do not keep Function*& aliases into argument slots across inference:
			// recursive regrouping may rebuild argument vectors, which would dangle references.
			Function *argument = expr->arguments[i];
			Function *updatedArgument = argument;
			if (!inferNestedForGrouping(argument, updatedArgument)) {
				expr = originalExpr;
				resetFunctionTypes(expr);
				context.addDiagnostic(
					{Diagnostic::Level::Error, buildTypeFailureDiagnostic(originalExpr, context.typeFailureDetail),
					 originalExpr->range}
				);
				return false;
			}
			expr->arguments[i] = updatedArgument;
		}
		if (!tryInfer()) {
			expr = originalExpr;
			resetFunctionTypes(expr);
			context.addDiagnostic(
				{Diagnostic::Level::Error, buildTypeFailureDiagnostic(originalExpr, context.typeFailureDetail),
				 originalExpr->range}
			);
			return false;
		}
		releaseOriginalExpr();
		return true;
	}
	std::vector<Function *> flatNodes;
	size_t operatorCount = 0;
	std::function<void(Function *, bool, bool)> collectFlatNodes = [&](Function *function, bool isOnBoundary, bool isRoot) {
		bool isPatternCall =
			function->kind == Function::Kind::PatternCall && !function->arguments.empty() && !function->isExplicitGroup;

		if (!isPatternCall) {
			flatNodes.push_back(function);
			return;
		}

		if (!isRoot &&
			(function->isExplicitGroup || !isOnBoundary || !function->isSubMatch || hasMultipleBoundaryArguments(function))) {
			Function *updatedFunction = function;
			if (!inferNestedForGrouping(function, updatedFunction)) {
				context.typesValid = false;
				return;
			}
			function = updatedFunction;
			flatNodes.push_back(function);
			return;
		}

		bool hasLeftEdge = startsWithArgument(function);
		bool hasRightEdge = endsWithArgument(function);

		if (!hasLeftEdge && !hasRightEdge) {
			for (size_t i = 0; i < function->arguments.size(); i++) {
				Function *argument = function->arguments[i];
				Function *updatedArgument = argument;
				if (!inferNestedForGrouping(argument, updatedArgument)) {
					context.typesValid = false;
					return;
				}
				function->arguments[i] = updatedArgument;
			}
			flatNodes.push_back(function);
			return;
		}

		if (hasMultipleBoundaryArguments(function)) {
			for (size_t i = 0; i < function->arguments.size(); i++) {
				Function *argument = function->arguments[i];
				Function *updatedArgument = argument;
				if (!inferNestedForGrouping(argument, updatedArgument)) {
					context.typesValid = false;
					return;
				}
				function->arguments[i] = updatedArgument;
			}
			flatNodes.push_back(function);
			return;
		}

		if (hasLeftEdge) {
			Function *left = function->arguments.front();
			collectFlatNodes(left, true, false);
			function->arguments.front() = left;
		}

		for (size_t i = (hasLeftEdge ? 1 : 0); i < function->arguments.size() - (hasRightEdge ? 1 : 0); i++) {
			Function *argument = function->arguments[i];
			Function *updatedArgument = argument;
			if (!inferNestedForGrouping(argument, updatedArgument)) {
				context.typesValid = false;
				return;
			}
			function->arguments[i] = updatedArgument;
		}

		operatorCount++;
		flatNodes.push_back(function);

		if (hasRightEdge) {
			Function *right = function->arguments.back();
			collectFlatNodes(right, true, false);
			function->arguments.back() = right;
		}
	};

	collectFlatNodes(expr, true, true);
	if (!context.typesValid) {
		expr = originalExpr;
		resetFunctionTypes(expr);
		context.addDiagnostic(
			{Diagnostic::Level::Error, buildTypeFailureDiagnostic(originalExpr, context.typeFailureDetail), originalExpr->range}
		);
		return false;
	}
	if (operatorCount <= 1) {
		if (!tryInfer()) {
			context.addDiagnostic(
				{Diagnostic::Level::Error, buildTypeFailureDiagnostic(originalExpr, context.typeFailureDetail),
				 originalExpr->range}
			);
			releaseOriginalExpr();
			return false;
		}
		releaseOriginalExpr();
		return true;
	}

	size_t ambiguousOperatorCount = operatorCount;
	if (expr && expr->kind == Function::Kind::PatternCall && mustOwnEntireRange(expr) && ambiguousOperatorCount > 0)
		ambiguousOperatorCount--;
	if (ambiguousOperatorCount > 8) {
		context.addDiagnostic({Diagnostic::Level::Error, "Too many ambiguous operand groupings", expr->range});
		releaseOriginalExpr();
		return false;
	}

	std::function<bool(int, int, std::function<bool(Function *)>)> tryGroupings =
		[&](int start, int end, std::function<bool(Function *)> onResult) -> bool {
		if (start > end)
			return false;
		if (start == end)
			return onResult(flatNodes[start]);

		Function *mandatoryRoot = flatNodes[start];
		bool rangeStartsWithMandatoryPrefix = mandatoryRoot->kind == Function::Kind::PatternCall &&
											  !mandatoryRoot->arguments.empty() && !mandatoryRoot->isExplicitGroup &&
											  !startsWithArgument(mandatoryRoot) && endsWithArgument(mandatoryRoot) &&
											  mustOwnEntireRange(mandatoryRoot);

		for (int rootIndex = end; rootIndex >= start; rootIndex--) {
			Function *rootFunction = flatNodes[rootIndex];
			if (rangeStartsWithMandatoryPrefix && rootIndex != start)
				continue;

			bool isPatternCall = rootFunction->kind == Function::Kind::PatternCall && !rootFunction->arguments.empty() &&
								 !rootFunction->isExplicitGroup;
			if (!isPatternCall)
				continue;
			bool hasLeftEdge = startsWithArgument(rootFunction);
			bool hasRightEdge = endsWithArgument(rootFunction);
			if (hasLeftEdge && rootIndex == start)
				continue;
			if (hasRightEdge && rootIndex == end)
				continue;
			if (!hasLeftEdge && rootIndex > start)
				continue;
			if (!hasRightEdge && rootIndex < end)
				continue;
			int rootPrecedence = functionPrecedence(rootFunction);
			if (rootPrecedence > 0 && hasLeftEdge && hasRightEdge) {
				bool lowerPrecedenceExists = false;
				for (int otherIndex = start; otherIndex <= end; otherIndex++) {
					if (otherIndex == rootIndex)
						continue;
					Function *otherFunction = flatNodes[otherIndex];
					if (otherFunction->kind != Function::Kind::PatternCall || otherFunction->arguments.empty() ||
						otherFunction->isExplicitGroup)
						continue;
					if (!startsWithArgument(otherFunction) || !endsWithArgument(otherFunction))
						continue;
					int otherPrecedence = functionPrecedence(otherFunction);
					if (otherPrecedence > 0 && otherPrecedence < rootPrecedence) {
						lowerPrecedenceExists = true;
						break;
					}
				}
				if (lowerPrecedenceExists)
					continue;
			}

			Function *savedLeft = hasLeftEdge ? rootFunction->arguments.front() : nullptr;
			Function *savedRight = hasRightEdge ? rootFunction->arguments.back() : nullptr;
			auto tryRight = [&](void) -> bool {
				if (!hasRightEdge)
					return onResult(rootFunction);
				return tryGroupings(rootIndex + 1, end, [&](Function *rightResult) -> bool {
					rootFunction->arguments.back() = rightResult;
					return onResult(rootFunction);
				});
			};
			bool done = false;
			if (hasLeftEdge) {
				done = tryGroupings(start, rootIndex - 1, [&](Function *leftResult) -> bool {
					rootFunction->arguments.front() = leftResult;
					return tryRight();
				});
			} else {
				done = tryRight();
			}
			if (done)
				return true;
			if (hasLeftEdge)
				rootFunction->arguments.front() = savedLeft;
			if (hasRightEdge)
				rootFunction->arguments.back() = savedRight;
		}
		return false;
	};

	int lastIndex = (int)flatNodes.size() - 1;
	std::string trialFailureDetail;
	bool found = tryGroupings(0, lastIndex, [&](Function *rootFunction) -> bool {
		expr = rootFunction;
		absorbOperatorIntoBoundaryArgument(expr);
		recomputeRanges(expr);
		sortArgumentsRecursive(expr);
		resetFunctionTypes(expr);
		InferenceContext::TrialJournal journal;
		InferenceContext trialContext(context.parseContext, true);
		trialContext.currentInstantiation = context.currentInstantiation;
		trialContext.trialJournal = &journal;
		inferOrderedFunction(expr, trialContext, macroBindings);
		if (!trialContext.typesValid && trialFailureDetail.empty()) {
			if (!trialContext.typeFailureDetail.empty())
				trialFailureDetail = trialContext.typeFailureDetail;
			else
				trialFailureDetail =
					"Unable to infer candidate grouping rooted at '" + (std::string)expr->range.subString + "'";
		}
		rollbackTrialJournal(journal);
		return trialContext.typesValid;
	});

	if (found) {
		recomputeRanges(expr);
		sortArgumentsRecursive(expr);
		resetFunctionTypes(expr);
		inferOrderedFunction(expr, context, macroBindings);
		if (context.typesValid)
			snapshotFunctionVariableReferences(expr, context);
		releaseOriginalExpr();
		return context.typesValid;
	}

	expr = originalExpr;
	resetFunctionTypes(expr);
	context.addDiagnostic(
		{Diagnostic::Level::Error, buildTypeFailureDiagnostic(originalExpr, trialFailureDetail), originalExpr->range}
	);
	return false;
}
