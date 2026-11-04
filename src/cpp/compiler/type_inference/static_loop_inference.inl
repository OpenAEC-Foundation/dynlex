// Meta-values can select different expressions and types on each iteration.
// Elaborate their ordinary loop bodies before code generation instead of merging
// those compile-time bindings into an unknown runtime index.
static bool inferStaticLoopIterations(
	Section *section, InstantiatedSectionBody *body, Expression *openingExpression, InferenceContext &context,
	const BindingFrameStack &bindings, size_t startLineIndex, bool &fallsThrough
) {
	const bool *entry = std::get_if<bool>(&openingExpression->sectionOutcome.read().conditionValue);
	if (!entry || !*entry) {
		context.setTypeFailure("A loop over argument references requires a compile-time condition");
		return false;
	}
	openingExpression->staticLoopBodies.emplace();
	openingExpression->staticLoopBodies->push_back(
		body ? body->shared_from_this() : context.cloneSectionBody(section, nullptr, {}, true)
	);
	if (!fallsThrough)
		return true;
	for (size_t iteration = 1;; iteration++) {
		if (iteration == 1000000) {
			context.setTypeFailure("Compile-time evaluation exceeded the step limit");
			return false;
		}
		Expression::SectionOutcome next;
		if (!inferNextLoopHeaderOutcomeInTrial(openingExpression, context, bindings, next, true))
			return false;
		const bool *condition = std::get_if<bool>(&next.conditionValue);
		if (!condition) {
			context.setTypeFailure("A loop over argument references requires a compile-time condition");
			return false;
		}
		if (!*condition)
			return true;
		auto scope =
			context.parseContext.storageScopeForInvocation(openingExpression, iteration, body ? body->storageScope : nullptr);
		auto iterationBody = context.cloneSectionBody(section, body ? body->parentBody : nullptr, std::move(scope));
		if (!inferSectionLineRange(
				section, iterationBody.get(), openingExpression, context, bindings, startLineIndex, true, &fallsThrough, false
			))
			return false;
		openingExpression->staticLoopBodies->push_back(std::move(iterationBody));
		if (!fallsThrough)
			return true;
	}
}
