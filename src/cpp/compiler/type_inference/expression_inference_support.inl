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

	void inferIntrinsic();
};
