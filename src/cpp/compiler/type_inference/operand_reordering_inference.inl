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
			originalDiagnostic.text,
			std::move(chosenGrouping),
			std::move(alternativeGrouping),
			context.captureInferenceTraceRelatedInfo(expr),
		});
	};
	auto emitOwnedGroupingWarnings = [&]() {
		if (!ownsPendingOperandGroupingWarnings || context.trial)
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
		ScopedRecursiveInferenceObservation expressionObservation(context, context.currentInstantiation);
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
			if (standaloneExpressionHasNonVoidResult(expr, context, expressionObservation.ownerObserved())) {
				context.fail(
					buildFailureDetailDiagnostic(
						originalDiagnostic.range, "Standalone expression '" + std::string(expr->range.subString) +
													  "' must return nothing; use ignore if you want to ignore a value"
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
	auto promoteSelectedTransaction = [&](std::unique_ptr<GroupingInferenceTransaction> &transaction) {
		requireCompilerInvariant(transaction != nullptr, "missing successful grouping inference transaction");
		applyGroupingSnapshot(selectedGrouping);
		expr = selectedGrouping.root;
		recomputeRanges(expr);
		transaction->promote();
		transaction.reset();
		if (context.resolvedGroupingRoots)
			context.resolvedGroupingRoots->insert(selectedFixedGroupingRoots.begin(), selectedFixedGroupingRoots.end());
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
		std::unique_ptr<GroupingInferenceTransaction> acceptedTransaction;
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
				&resolvedGroupingRoots, &candidateGrouping, &candidateGroupingWarnings, &acceptedTransaction
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
			if (acceptedTransaction) {
				promoteSelectedTransaction(acceptedTransaction);
				queueSelectedGroupingWarnings();
				emitOwnedGroupingWarnings();
				return true;
			}
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

	std::unique_ptr<GroupingInferenceTransaction> lastAcceptedTransaction;
	enumerateExpressionGroupings(
		expr, context, alreadyOrdered, flexBindingFrameStack,
		[&](Expression *&candidateExpr,
			const std::unordered_set<Expression *> &fixedGroupingRoots) -> GroupingEnumerationProgress {
		if (lastAcceptedTransaction) {
			GroupingSnapshot nextCandidateGrouping = captureGroupingSnapshot(candidateExpr);
			lastAcceptedTransaction->rollback(expr);
			lastAcceptedTransaction.reset();
			applyGroupingSnapshot(nextCandidateGrouping);
			expr = nextCandidateGrouping.root;
			candidateExpr = expr;
			recomputeRanges(expr);
			resetExpressionTypes(expr);
		}
		expr = candidateExpr;
		std::unordered_set<Expression *> resolvedGroupingRoots;
		GroupingSnapshot candidateGrouping;
		std::vector<InferenceContext::OperandGroupingWarning> candidateGroupingWarnings;
		std::unique_ptr<GroupingInferenceTransaction> candidateTransaction;
		bool accepted = validateGroupingInTrial(
			expr, context, fixedGroupingRoots, flexBindingFrameStack, originalDiagnostic, requireVoidResult, &trialFailure,
			&resolvedGroupingRoots, &candidateGrouping, &candidateGroupingWarnings, &candidateTransaction
		);
		if (!accepted)
			return GroupingEnumerationProgress::EmittedContinue;

		applyGroupingSnapshot(candidateGrouping);
		expr = candidateGrouping.root;
		std::string candidateRendered = renderResolvedExpression(expr);

		if (!foundValidGrouping) {
			selectedGrouping = std::move(candidateGrouping);
			selectedGroupingWarnings = std::move(candidateGroupingWarnings);
			storedValidRendered = candidateRendered;
			selectedFixedGroupingRoots = std::move(resolvedGroupingRoots);
			foundValidGrouping = true;
			lastAcceptedTransaction = std::move(candidateTransaction);
			return GroupingEnumerationProgress::EmittedContinue;
		}
		if (snapshotsHaveSameLocalOrdering(candidateGrouping, selectedGrouping)) {
			selectedGrouping = std::move(candidateGrouping);
			selectedGroupingWarnings = std::move(candidateGroupingWarnings);
			selectedFixedGroupingRoots = std::move(resolvedGroupingRoots);
			lastAcceptedTransaction = std::move(candidateTransaction);
			return GroupingEnumerationProgress::EmittedContinue;
		}
		candidateTransaction->rollback(expr);
		candidateTransaction.reset();
		ambiguousAlternativeRendered = candidateRendered;
		groupingAmbiguous = true;
		return GroupingEnumerationProgress::Stop;
	}
	);

	if (foundValidGrouping) {
		if (lastAcceptedTransaction) {
			promoteSelectedTransaction(lastAcceptedTransaction);
			queueSelectedGroupingWarnings();
			emitOwnedGroupingWarnings();
			return true;
		}
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
