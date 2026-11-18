static bool sameGroupingState(const GroupingState &left, const GroupingState &right) {
	return left == right || (left && right && *left == *right);
}

static GroupingState currentGroupingState(CodeLine *line, const InferenceContext *context) {
	if (context && context->trial) {
		if (const auto *grouping = context->groupingForLine(line))
			return grouping->state;
	}
	return line->groupingState;
}

static void recordGroupingTransition(InferenceContext &context, CodeLine *line, GroupingTransition transition) {
	if (context.parseContext.inferenceProducts)
		context.groupingTrace =
			std::make_shared<GroupingTrace>(GroupingTrace{context.groupingTrace, line, std::move(transition)});
}

static std::shared_ptr<const GroupingProduct> captureGroupingProduct(
	const std::shared_ptr<const GroupingTrace> &end, const std::shared_ptr<const GroupingTrace> &begin,
	const std::shared_ptr<const VariableStorageScope> &instance
) {
	auto product = std::make_shared<GroupingProduct>();
	for (auto cursor = end; cursor != begin; cursor = cursor->previous) {
		requireCompilerInvariant(cursor != nullptr, "grouping query lost its entry checkpoint");
		if (cursor->kind != GroupingTrace::Kind::Grouping) {
			product->providedInstances.insert(cursor->instance);
			if (cursor->kind == GroupingTrace::Kind::ProvidedInstance)
				product->requiredInstances.erase(cursor->instance);
			else
				product->requiredInstances.insert(cursor->instance);
			continue;
		}
		auto [entry, inserted] = product->groupings.try_emplace(cursor->line, cursor->transition);
		if (!inserted)
			entry->second.input = cursor->transition.input;
	}
	if (instance && (!product->groupings.empty() || !product->requiredInstances.empty() || !product->providedInstances.empty()))
		product->providedInstances.insert(instance);
	return product;
}

static void observeDiscardedGroupings(const GroupingProduct &product, InferenceContext &context) {
	for (const auto &[line, _] : product.groupings) {
		auto state = currentGroupingState(line, &context);
		recordGroupingTransition(context, line, {state, state});
	}
	for (const auto &scope : product.requiredInstances)
		recordInferenceInstance(context, scope, false);
}

bool inferenceInstanceAvailable(
	ParseContext &parseContext, const std::shared_ptr<const VariableStorageScope> &scope, const InferenceContext *context
) {
	for (auto *cursor = context; cursor && cursor->trial; cursor = cursor->inheritedGroupingContext)
		if (cursor->trialAvailableInferenceInstances.contains(scope))
			return true;
	return parseContext.availableInferenceInstances.contains(scope);
}

void recordInferenceInstance(
	InferenceContext &context, const std::shared_ptr<const VariableStorageScope> &scope, bool provided
) {
	if (!context.parseContext.inferenceProducts)
		return;
	requireCompilerInvariant(scope != nullptr, "inferred instance has no storage identity");
	if (provided) {
		if (context.trial) {
			if (context.groupingTrialJournal)
				context.groupingTrialJournal->recordAvailabilityWrite(context.trialAvailableInferenceInstances, scope);
			context.trialAvailableInferenceInstances.insert(scope);
		} else
			context.parseContext.availableInferenceInstances.insert(scope);
	} else
		requireCompilerInvariant(
			inferenceInstanceAvailable(context.parseContext, scope, &context), "inference consumed an unavailable instance"
		);
	context.groupingTrace = std::make_shared<GroupingTrace>(GroupingTrace{
		.previous = context.groupingTrace,
		.transition = {},
		.instance = scope,
		.kind = provided ? GroupingTrace::Kind::ProvidedInstance : GroupingTrace::Kind::RequiredInstance
	});
}

static void observeGroupingProbe(const InferenceContext &probe, InferenceContext &caller) {
	if (probe.groupingTrace != caller.groupingTrace)
		observeDiscardedGroupings(*captureGroupingProduct(probe.groupingTrace, caller.groupingTrace), caller);
}

static void prepareGroupingSourceNodes(CodeLine *line) {
	if (!line->groupingSourceNodes.empty())
		return;
	ExpressionNodeSet visited;
	auto visit = [&](auto &&self, Expression *expression) -> void {
		if (!visited.insert(expression).second)
			return;
		line->groupingSourceNodes.push_back(expression);
		for (Expression *argument : unconvertedExpression(expression)->arguments)
			self(self, argument);
	};
	visit(visit, line->expression);
	line->groupingDependencyTracked = std::ranges::count_if(line->groupingSourceNodes, [](Expression *expression) {
		return expressionHasGroupingShape(expression);
	}) > 1;
}

static GroupingSnapshot materializeGrouping(CodeLine *line, const GroupingState &state) {
	prepareGroupingSourceNodes(line);
	requireCompilerInvariant(
		state && state->nodes.size() == line->groupingSourceNodes.size(), "cached grouping lost its source nodes"
	);
	GroupingSnapshot snapshot;
	snapshot.root = line->groupingSourceNodes.at(state->root);
	for (size_t index = 0; index < state->nodes.size(); index++) {
		const auto &node = state->nodes[index];
		std::vector<Expression *> arguments;
		for (size_t argument : node.arguments)
			arguments.push_back(line->groupingSourceNodes.at(argument));
		snapshot.nodes.emplace(
			line->groupingSourceNodes[index], GroupingSnapshot::NodeState{std::move(arguments), node.explicitGroup}
		);
	}
	return snapshot;
}

static GroupingState captureGroupingLayout(CodeLine *line, const GroupingSnapshot &snapshot, bool checked) {
	auto layout = std::make_shared<GroupingLayout>();
	std::unordered_map<Expression *, size_t> indices;
	for (size_t index = 0; index < line->groupingSourceNodes.size(); index++)
		indices.emplace(line->groupingSourceNodes[index], index);
	layout->root = indices.at(snapshot.root);
	layout->ambiguityChecked = checked;
	for (Expression *expression : line->groupingSourceNodes) {
		const auto &node = snapshot.nodes.at(expression);
		std::vector<size_t> arguments;
		for (Expression *argument : node.arguments)
			arguments.push_back(indices.at(argument));
		layout->nodes.push_back({std::move(arguments), node.explicitGroup});
	}
	return layout;
}

static void observeCodeLineGrouping(CodeLine *line, InferenceContext &context) {
	if (!context.parseContext.inferenceProducts)
		return;
	if (line->groupingDependencyTracked == false)
		return;
	prepareGroupingSourceNodes(line);
	if (!*line->groupingDependencyTracked) {
		line->groupingSourceNodes.clear();
		return;
	}
	auto state = currentGroupingState(line, &context);
	recordGroupingTransition(context, line, {state, state});
}

static void observeGroupingResult(const Instantiation &instance, InferenceContext &context) {
	if (!context.parseContext.inferenceProducts || !instance.groupingProduct)
		return;
	// A completed query carries its completed callees as well. Observing only
	// this instance would publish different availability effects depending on
	// whether its body ran here or an earlier caller had already inferred it.
	for (const auto &scope : instance.groupingProduct->providedInstances)
		recordInferenceInstance(context, scope, false);
	for (const auto &[line, transition] : instance.groupingProduct->groupings) {
		// Absence is an input observation, not a published source choice. The
		// concrete instance prerequisite preserves its original producer.
		if (!transition.output)
			continue;
		auto state = currentGroupingState(line, &context);
		bool checkedLater = state && transition.output && state->ambiguityChecked && !transition.output->ambiguityChecked &&
							state->root == transition.output->root && state->nodes == transition.output->nodes;
		requireCompilerInvariant(
			sameGroupingState(state, transition.output) || checkedLater, "inferred grouping result changed within a trial"
		);
		recordGroupingTransition(context, line, {state, state});
	}
}

bool validateGroupingProduct(
	ParseContext &parseContext, const std::shared_ptr<const GroupingProduct> &product, InferenceContext *context
) {
	for (const auto &scope : product->requiredInstances)
		if (!inferenceInstanceAvailable(parseContext, scope, context))
			return false;
	for (const auto &[line, transition] : product->groupings)
		if (!sameGroupingState(currentGroupingState(line, context), transition.input))
			return false;
	return true;
}

bool supportsGroupingQuery(const Instantiation &instance, const InferenceContext *context) {
	requireCompilerInvariant(instance.groupingProduct != nullptr, "inferred query has no grouping product");
	return instance.groupingAmbiguityRequested || instance.groupingProduct->groupings.empty() ||
		   (context && context->trial && !context->detectGroupingAmbiguity);
}

bool applyGroupingProduct(
	ParseContext &parseContext, const std::shared_ptr<const GroupingProduct> &product, InferenceContext *context
) {
	if (!validateGroupingProduct(parseContext, product, context))
		return false;
	if (context)
		for (const auto &scope : product->requiredInstances)
			recordInferenceInstance(*context, scope, false);
	for (const auto &[line, transition] : product->groupings) {
		if (context)
			recordGroupingTransition(*context, line, transition);
		if (sameGroupingState(transition.input, transition.output))
			continue;
		requireCompilerInvariant(transition.output != nullptr, "grouping query removed a source grouping");
		if (context && context->trial) {
			if (context->groupingTrialJournal)
				context->groupingTrialJournal->recordCodeLineGroupingWrite(context->trialCodeLineGroupings, line);
			context->trialCodeLineGroupings[line] = {
				.grouping = {},
				.state = transition.output,
				.reusableTemplate = true,
				.ambiguityChecked = transition.output->ambiguityChecked
			};
		} else {
			line->groupingState = transition.output;
			line->committedGrouping = {};
			line->hasCommittedGrouping = true;
			line->groupingAmbiguityChecked = transition.output->ambiguityChecked;
		}
	}
	for (const auto &scope : product->providedInstances) {
		if (context)
			recordInferenceInstance(*context, scope, true);
		else
			parseContext.availableInferenceInstances.insert(scope);
	}
	return true;
}
