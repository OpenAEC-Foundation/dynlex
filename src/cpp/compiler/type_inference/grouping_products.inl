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

static std::shared_ptr<const GroupingProduct>
captureGroupingProduct(const std::shared_ptr<const GroupingTrace> &end, const std::shared_ptr<const GroupingTrace> &begin) {
	auto product = std::make_shared<GroupingProduct>();
	for (auto cursor = end; cursor != begin; cursor = cursor->previous) {
		requireCompilerInvariant(cursor != nullptr, "grouping query lost its entry checkpoint");
		auto [entry, inserted] = product->try_emplace(cursor->line, cursor->transition);
		if (!inserted)
			entry->second.input = cursor->transition.input;
	}
	return product;
}

static void observeDiscardedGroupings(const GroupingProduct &product, InferenceContext &context) {
	for (const auto &[line, _] : product) {
		auto state = currentGroupingState(line, &context);
		recordGroupingTransition(context, line, {state, state});
	}
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
	for (const auto &[line, transition] : *instance.groupingProduct) {
		auto state = currentGroupingState(line, &context);
		bool checkedLater = state && transition.output && state->ambiguityChecked && !transition.output->ambiguityChecked &&
							state->root == transition.output->root && state->nodes == transition.output->nodes;
		requireCompilerInvariant(
			sameGroupingState(state, transition.output) || checkedLater, "inferred grouping result changed within a trial"
		);
		recordGroupingTransition(context, line, {state, state});
	}
}

bool validateGroupingProduct(const std::shared_ptr<const GroupingProduct> &product, InferenceContext *context) {
	for (const auto &[line, transition] : *product)
		if (!sameGroupingState(currentGroupingState(line, context), transition.input))
			return false;
	return true;
}

bool applyGroupingProduct(ParseContext &, const std::shared_ptr<const GroupingProduct> &product, InferenceContext *context) {
	if (!validateGroupingProduct(product, context))
		return false;
	for (const auto &[line, transition] : *product) {
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
	return true;
}
