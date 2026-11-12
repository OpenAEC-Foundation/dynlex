// Possible first accesses on executable paths. Sequential composition replaces
// only paths which have not accessed the variable; branch joins preserve both.
struct VariableUsage {
	enum Access : unsigned { None = 1, Read = 2, Write = 4, Stopped = 8 };
	unsigned first = None;
	void then(VariableUsage other) { first = (first & ~None) | ((first & None) ? other.first : 0); }
	void either(VariableUsage other) { first |= other.first; }
};

static VariableUsage analyzeVariableUsage(
	Expression *expression, VariableReference *definition, bool incomingValue, const BindingFrameStack &bindings,
	std::unordered_set<Expression *> &active
);

static VariableUsage analyzeVariableUsageBody(
	InstantiatedSectionBody &body, VariableReference *definition, bool incomingValue, const BindingFrameStack &bindings,
	std::unordered_set<Expression *> &active, Expression *opening = nullptr
) {
	using Kind = Expression::SectionOutcome::Kind;
	const auto &lines = body.sourceSection->codeLines;
	auto expression = [&](size_t index) {
		return body.lineExpression(index);
	};
	auto child = [&](size_t index) -> VariableUsage {
		Section *section = lines[index]->sectionOpening;
		Expression *opener = expression(index);
		if (!section || !section->runsInEnclosingBody() || (opener && !opener->sectionBodyReachable))
			return {};
		InstantiatedSectionBody *nested = body.bodyForChild(section);
		requireCompilerInvariant(nested, "usage analysis lost an executable section body");
		return analyzeVariableUsageBody(*nested, definition, incomingValue, bindings, active, opener);
	};
	if (opening && opening->sectionOutcome.read().kind == Kind::Switch) {
		VariableUsage alternatives{0};
		bool hasDefault = false;
		for (size_t index = 0; index < lines.size(); index++) {
			Expression *branch = expression(index);
			if (!branch)
				continue;
			hasDefault |= branch->sectionOutcome.read().kind == Kind::DefaultCase;
			if (branch->sectionBodyReachable)
				alternatives.either(child(index));
		}
		bool unmatched = opening->branchSelection->known ? opening->branchSelection->selectedBranchIndex < 0 : !hasDefault;
		if (unmatched)
			alternatives.either({});
		return alternatives;
	}
	VariableUsage result;
	for (size_t index = 0; index < lines.size(); index++) {
		Expression *line = expression(index);
		if (line && !line->type.isDeduced())
			continue; // Inference did not execute this unreachable source line.
		Kind kind = line ? line->sectionOutcome.read().kind : Kind::None;
		if (kind == Kind::Conditional) {
			VariableUsage alternatives{0}, pending;
			size_t last = index;
			for (size_t branchIndex = index; branchIndex < lines.size(); branchIndex++) {
				Expression *branch = expression(branchIndex);
				if (!branch)
					break;
				const auto &outcome = branch->sectionOutcome.read();
				if (branchIndex > index && outcome.kind != Kind::AlternativeConditional && outcome.kind != Kind::Alternative)
					break;
				last = branchIndex;
				if (!pending.first)
					continue;
				pending.then(analyzeVariableUsage(branch, definition, incomingValue, bindings, active));
				if (branch->sectionBodyReachable) {
					VariableUsage taken = pending;
					taken.then(child(branchIndex));
					alternatives.either(taken);
				}
				const bool *condition = std::get_if<bool>(&outcome.conditionValue);
				if (outcome.kind == Kind::Alternative || (condition && *condition))
					pending.first = 0;
			}
			alternatives.either(pending);
			result.then(alternatives);
			index = last;
			continue;
		}
		result.then(analyzeVariableUsage(line, definition, incomingValue, bindings, active));
		VariableUsage opened = child(index);
		if (kind == Kind::Loop) {
			const bool *condition = std::get_if<bool>(&line->sectionOutcome.read().conditionValue);
			if (!condition || !*condition)
				opened.either({}); // The header can reject the first iteration.
		}
		result.then(opened);
		if (line && line->executionFallsThrough == false)
			result.then({VariableUsage::Stopped});
	}
	return result;
}

static VariableUsage analyzeVariableUsage(
	Expression *expression, VariableReference *definition, bool incomingValue, const BindingFrameStack &bindings,
	std::unordered_set<Expression *> &active
) {
	if (!expression)
		return {};
	requireCompilerInvariant(active.insert(expression).second, "cyclic concrete expression during variable usage analysis");
	struct Restore {
		std::unordered_set<Expression *> &active;
		Expression *expression;
		~Restore() { active.erase(expression); }
	} restore{active, expression};
	ResolvedBindingLayers resolved = resolveVariableBindingWithCallerScope(expression, bindings);
	if (resolved.expression != expression)
		return analyzeVariableUsage(resolved.expression, definition, incomingValue, resolved.bindingFrameStack, active);
	if (expression->kind == Expression::Kind::Variable) {
		requireCompilerInvariant(expression->variable, "typed variable usage has no declaration reference");
		return {normalizeBindingReference(expression->variable) == definition ? VariableUsage::Read : VariableUsage::None};
	}
	VariableUsage usage;
	if (expression->kind == Expression::Kind::IntrinsicCall &&
		intrinsicKind(expression->intrinsicName) == IntrinsicKind::Store) {
		// Native stores evaluate the value before computing the destination.
		usage.then(analyzeVariableUsage(expression->arguments[2], definition, incomingValue, bindings, active));
		auto destination = resolveExpressionBindingWithCallerScope(expression->arguments[1], bindings);
		if (destination.expression->kind == Expression::Kind::Variable) {
			requireCompilerInvariant(destination.expression->variable, "typed store destination has no declaration reference");
			if (normalizeBindingReference(destination.expression->variable) == definition) {
				// Replacing an initialized managed parameter releases its old
				// value before writing the replacement. That consumes the input.
				if (incomingValue && typeHasManagedLifecycle(destination.expression->type))
					usage.then({VariableUsage::Read});
				usage.then({VariableUsage::Write});
			}
		} else
			usage.then(
				analyzeVariableUsage(destination.expression, definition, incomingValue, destination.bindingFrameStack, active)
			);
		return usage;
	}
	if (expression->kind == Expression::Kind::PatternCall && expression->inferredFlexBody) {
		BindingFrameStack callBindings = bindings;
		pushPatternCallBindingScope(callBindings, expression, expression->selectedPatternDefinition);
		return analyzeVariableUsageBody(*expression->inferredFlexBody, definition, incomingValue, callBindings, active);
	}
	for (Expression *argument : expression->arguments)
		usage.then(analyzeVariableUsage(argument, definition, incomingValue, bindings, active));
	if (expression->kind == Expression::Kind::IntrinsicCall &&
		intrinsicKind(expression->intrinsicName) == IntrinsicKind::Return)
		usage.then({VariableUsage::Stopped});
	return usage;
}
