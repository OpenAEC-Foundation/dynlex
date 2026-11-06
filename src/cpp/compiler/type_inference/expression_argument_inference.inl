bool ExpressionInferenceSupport::inferArgument(
	size_t index, bool preserveCurrentGrouping, InferredArgumentScope &argumentScope
) {
	struct ReferenceTargetScope {
		InferenceContext &context;
		bool previous;
		~ReferenceTargetScope() { context.inferringReferenceAssignmentTarget = previous; }
	} targetScope{context, context.inferringReferenceAssignmentTarget};
	context.inferringReferenceAssignmentTarget = expr->kind == Expression::Kind::IntrinsicCall &&
												 intrinsicKind(expr->intrinsicName) == IntrinsicKind::Store && index == 1;
	Expression *arg = expr->arguments[index];
	std::optional<InferenceExecutionSnapshot> beforeArgument;
	if (expr->kind == Expression::Kind::PatternCall)
		beforeArgument.emplace(context);
	bool preserveArgumentGrouping =
		preserveCurrentGrouping && (!context.fixedGroupingRoots || context.fixedGroupingRoots->contains(arg));
	bool inferred = preserveArgumentGrouping ? inferExpressionWithCurrentGrouping(arg, context, flexBindingFrameStack)
											 : inferExpression(arg, context, false, flexBindingFrameStack);
	expr->arguments[index] = arg;
	if (inferred && beforeArgument && beforeArgument->matches(context))
		argumentScope.unchanged.emplace(arg, std::move(*beforeArgument));
	return inferred;
}

bool ExpressionInferenceSupport::inferConditionalArguments(
	size_t firstBranch, const ConditionalOperands &conditional, bool preserveCurrentGrouping,
	InferredArgumentScope &argumentScope
) {
	InferenceExecutionSnapshot branchInput(context);
	CompileTimeValue conditionValue = resolveStoredCompileTimeValue(expr->arguments[1], flexBindingFrameStack, &context);
	const bool *condition = std::get_if<bool>(&conditionValue);
	std::vector<KnownConstantState> values;
	std::vector<AddressInferenceState> addresses;
	std::vector<InferenceContext::SubjectState> subjects;
	auto rememberPath = [&] {
		values.push_back(context.currentVariableValues);
		addresses.push_back(context.currentAddressState);
		subjects.push_back(context.currentSubject);
	};
	if ((!condition && (!conditional.whenFalse || !conditional.whenTrue)) || (condition && !conditional.selected(*condition)))
		rememberPath();
	for (size_t branch = firstBranch; branch < expr->arguments.size(); branch++) {
		branchInput.restoreState(context);
		InferenceExecutionSnapshot beforeBranch(context);
		if (!inferArgument(branch, preserveCurrentGrouping, argumentScope))
			return false;
		if (!condition || conditional.selected(*condition) == branch)
			rememberPath();
		else
			beforeBranch.restore(context);
	}
	mergeExecutionStates(context, values, addresses, subjects);
	return true;
}
