// A replacement result includes its complete emitted bodies, not just the
// expression producing its final value. Grouping trials can reset a statement
// inside one of those bodies without changing that final expression.
struct InferenceResultWalk {
	const std::function<bool(Expression *)> &visitor;
	std::unordered_set<Expression *> expressions;
	std::unordered_set<InstantiatedSectionBody *> bodies;

	bool body(InstantiatedSectionBody *value) {
		if (!value || !bodies.insert(value).second)
			return false;
		for (Expression *line : value->lineExpressions)
			if (expression(line))
				return true;
		for (const auto &child : value->childBodies)
			if (body(child.get()))
				return true;
		return false;
	}
	bool expression(Expression *value) {
		if (!value || !expressions.insert(value).second)
			return false;
		if (visitor(value))
			return true;
		for (Expression *argument : value->arguments)
			if (expression(argument))
				return true;
		if (value->inferredArgumentReference) {
			const auto &capture = *value->inferredArgumentReference;
			requireCompilerInvariant(capture.data != nullptr, "inferred argument reference has no source capture");
			if (expression(capture.data->expression()))
				return true;
		}
		if (expression(value->inferredFlexExpansion) || expression(value->inferredDataValue) ||
			body(value->inferredFlexBody.get()) || body(value->inferredCallerBody.get()))
			return true;
		if (value->staticLoopBodies)
			for (const auto &iteration : *value->staticLoopBodies)
				if (body(iteration.get()))
					return true;
		return false;
	}
};

// The preliminary argument walk learns types and a transition of execution
// state. A replacement can reuse that transition when it reaches the same
// input, rather than recursively repeating speculative inference.
struct InferredArgumentScope {
	struct ArgumentInference {
		struct NodeState {
			std::uint64_t revision;
			DataType type;
			CompileTimeValue value;
			std::vector<Expression *> arguments;
			Expression *expansion;
			Expression *data;
			std::optional<ExpressionCapture> reference;
		};
		InferenceExecutionSnapshot input;
		InferenceExecutionSnapshot output;
		BindingFrameStack bindings;
		bool assignmentTarget;
		std::unordered_map<Expression *, NodeState> nodes;

		ArgumentInference(
			Expression *expression, InferenceExecutionSnapshot before, const InferenceContext &context,
			const BindingFrameStack &bindings
		)
			: input(std::move(before)), output(context), bindings(bindings),
			  assignmentTarget(context.inferringReferenceAssignmentTarget) {
			// Types are learned by the preliminary walk and are not rolled back
			// when its execution effects are restored.
			input.types = output.types;
			// Compare the tree's inferred result, not a variable's current
			// execution value: replay starts from the saved input state.
			std::function<bool(Expression *)> capture = [&](Expression *node) {
				nodes.emplace(
					node, NodeState{
							  node->inferenceRevision, node->type, context.lookupStoredExpressionValue(node), node->arguments,
							  node->inferredFlexExpansion, node->inferredDataValue, node->inferredArgumentReference
						  }
				);
				return false;
			};
			InferenceResultWalk walk{capture, {}, {}};
			walk.expression(expression);
		}

		bool stillDescribes(Expression *expression, const InferenceContext &context) const {
			std::function<bool(Expression *)> compare = [&](Expression *node) {
				auto found = nodes.find(node);
				if (found == nodes.end())
					return true;
				const NodeState &saved = found->second;
				return saved.revision != node->inferenceRevision || saved.type != node->type ||
					   saved.value != context.lookupStoredExpressionValue(node) || saved.arguments != node->arguments ||
					   saved.expansion != node->inferredFlexExpansion || saved.data != node->inferredDataValue ||
					   saved.reference != node->inferredArgumentReference;
			};
			InferenceResultWalk walk{compare, {}, {}};
			return !walk.expression(expression) && walk.expressions.size() == nodes.size();
		}
	};

	InferenceContext &context;
	const InferredArgumentScope *parent;
	std::unordered_map<Expression *, ArgumentInference> arguments;

	explicit InferredArgumentScope(InferenceContext &context) : context(context), parent(context.inferredArguments) {
		context.inferredArguments = this;
	}
	~InferredArgumentScope() { context.inferredArguments = parent; }

	void remember(Expression *expression, InferenceExecutionSnapshot input, const BindingFrameStack &bindings) {
		arguments.emplace(expression, ArgumentInference(expression, std::move(input), context, bindings));
	}

	bool replay(Expression *expression, const BindingFrameStack &bindings) const {
		for (auto *scope = this; scope; scope = scope->parent) {
			auto found = scope->arguments.find(expression);
			if (found == scope->arguments.end())
				continue;
			const ArgumentInference &argument = found->second;
			if (argument.assignmentTarget != context.inferringReferenceAssignmentTarget ||
				!argument.bindings.sharesFramesWith(bindings) || !argument.input.matches(context) ||
				!argument.stillDescribes(expression, context))
				continue;
			argument.output.restore(context);
			return true;
		}
		return false;
	}
};
