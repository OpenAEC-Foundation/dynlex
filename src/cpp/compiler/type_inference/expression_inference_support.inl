struct ExpressionInferenceSupport {
	Expression *expr;
	InferenceContext &context;
	const BindingFrameStack &flexBindingFrameStack;

	Expression *resolveThroughFlexBindings(Expression *expression) const {
		return resolveInferenceBindingLayers(expression, flexBindingFrameStack, &context).expression;
	}

	void setConfiguredTypeFailure(
		Range range, std::string_view key, std::string_view variant = "message",
		std::vector<std::pair<std::string, std::string>> replacements = {}
	) const {
		context.setTypeFailure(
			renderConfiguredMessage(syntaxConfigForRange(context.parseContext, range), key, variant, replacements)
		);
	}

	void failWithDetail(Range range, const std::string &detail, int priority = 1) const {
		context.setTypeFailure(detail);
		context.fail(buildFailureDetailDiagnostic(range, detail), priority);
	}

	void failConfigured(
		Range range, std::string_view key, std::string_view variant = "message",
		std::vector<std::pair<std::string, std::string>> replacements = {}
	) const {
		failWithDetail(
			range, renderConfiguredMessage(syntaxConfigForRange(context.parseContext, range), key, variant, replacements)
		);
	}

	void failIntrinsicArgumentRequirement(size_t argumentIndex, std::string_view requirement) const {
		if (argumentIndex >= expr->arguments.size() || !expr->arguments[argumentIndex])
			crashCompilerBug("intrinsic argument validation encountered a missing argument expression");
		std::string detail = "Intrinsic '" + expr->intrinsicName + "' argument " + std::to_string(argumentIndex) + " must be " +
							 std::string(requirement);
		failWithDetail(expr->arguments[argumentIndex]->range, detail, 0);
	}

	void failCompileTimeOnlyIntrinsicArgument(size_t argumentIndex, std::string_view requirement) const {
		failIntrinsicArgumentRequirement(argumentIndex, requirement);
	}

	bool inferArgument(size_t index, bool preserveCurrentGrouping, InferredArgumentScope &argumentScope);
	// Keep node-specific temporaries outside the recursive traversal frame.
	[[gnu::noinline]] bool inferConditionalArguments(
		size_t firstBranch, const ConditionalOperands &conditional, bool preserveCurrentGrouping,
		InferredArgumentScope &argumentScope
	);
	[[gnu::noinline]] void inferLiteral();
	[[gnu::noinline]] void inferArgumentPack();
	[[gnu::noinline]] bool inferArrayLiteral();
	[[gnu::noinline]] bool inferVariable();
	[[gnu::noinline]] void inferArithmeticIntrinsic(IntrinsicKind kind);
	[[gnu::noinline]] void inferBitwiseIntrinsic();
	[[gnu::noinline]] void inferBooleanIntrinsic(IntrinsicKind kind);
	[[gnu::noinline]] bool validateIntrinsic(IntrinsicKind kind, const IntrinsicInfo *info);
	[[gnu::noinline]] void inferVoidIntrinsic(IntrinsicKind kind);
	[[gnu::noinline]] void inferShaderInputIntrinsic();
	[[gnu::noinline]] void inferNumberIntrinsic();
	[[gnu::noinline]] void inferElementTypeIntrinsic();
	[[gnu::noinline]] void inferPromoteArithmeticTypeIntrinsic();
	[[gnu::noinline]] void inferElementIntrinsic(IntrinsicKind kind);
	[[gnu::noinline]] void inferAtomicIntrinsic(IntrinsicKind kind);
	[[gnu::noinline]] void inferLifecycleValueIntrinsic();
	[[gnu::noinline]] void inferSubjectIntrinsic();
	[[gnu::noinline]] void inferCommandLineArgumentsIntrinsic(IntrinsicKind kind);
	[[gnu::noinline]] void inferAddressOfIntrinsic();
	[[gnu::noinline]] void inferDereferenceIntrinsic();
	[[gnu::noinline]] void inferExternalCallIntrinsic(IntrinsicKind kind);
	[[gnu::noinline]] void inferFunctionIntrinsic();
	[[gnu::noinline]] void inferCastIntrinsic();
	[[gnu::noinline]] void inferTypeIntrinsic();
	[[gnu::noinline]] void inferNumberRuleIntrinsic();
	[[gnu::noinline]] void inferFixIntrinsic();
	[[gnu::noinline]] void inferTypeOfIntrinsic();
	[[gnu::noinline]] void inferTypeExtentIntrinsic();
	[[gnu::noinline]] void inferSelectIntrinsic();
	[[gnu::noinline]] void inferSizeOfIntrinsic();
	[[gnu::noinline]] void inferBuildInfoIntrinsic();
	[[gnu::noinline]] void inferTargetIsIntrinsic();
	[[gnu::noinline]] void inferShaderStageIsIntrinsic();
	[[gnu::noinline]] void inferArrayIntrinsic();
	[[gnu::noinline]] void inferVectorIntrinsic();
	[[gnu::noinline]] void inferMatrixIntrinsic();
	[[gnu::noinline]] void inferAddPointerDepthIntrinsic();
	[[gnu::noinline]] void inferConstructIntrinsic();
	[[gnu::noinline]] void inferPropertyIntrinsic();
	[[gnu::noinline]] void inferCustomIntrinsic(IntrinsicKind kind);
	[[gnu::noinline]] void inferIntrinsic();
};
