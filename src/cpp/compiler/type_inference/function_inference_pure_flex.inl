struct ScopedPureActiveBody {
	PureExecutionState &state;

	ScopedPureActiveBody(PureExecutionState &executionState, InstantiatedSectionBody *body) : state(executionState) {
		requireCompilerInvariant(body != nullptr, "pure execution cannot enter a missing inferred body");
		state.activeBodies.push_back(body);
	}

	~ScopedPureActiveBody() {
		requireCompilerInvariant(!state.activeBodies.empty(), "pure execution active-body stack underflowed");
		state.activeBodies.pop_back();
	}
};

struct ScopedPureFlexLocalValues {
	struct Entry {
		VariableStorageKey storage;
		std::optional<CompileTimeValue> previousValue;
	};

	PureExecutionFrame &frame;
	std::vector<Entry> entries;

	void enterBody(InstantiatedSectionBody *body) {
		requireCompilerInvariant(body && body->sourceSection, "pure local scope has no source section");
		for (const auto &[name, definition] : body->sourceSection->variableDefinitions) {
			(void)name;
			VariableStorageKey storage = variableStorageKey(definition, body);
			auto previous = frame.localValues.find(storage);
			entries.push_back(
				{storage,
				 previous == frame.localValues.end() ? std::nullopt : std::optional<CompileTimeValue>(previous->second)}
			);
			frame.localValues[storage] = CompileTimeValue{};
		}
		for (const auto &child : body->childBodies)
			enterBody(child.get());
	}

	ScopedPureFlexLocalValues(PureExecutionFrame &executionFrame, InstantiatedSectionBody *body) : frame(executionFrame) {
		enterBody(body);
	}

	~ScopedPureFlexLocalValues() {
		for (Entry &entry : entries) {
			if (entry.previousValue)
				frame.localValues[entry.storage] = std::move(*entry.previousValue);
			else
				frame.localValues.erase(entry.storage);
		}
	}
};

struct ScopedPureFlexExecution {
	PureExecutionState &state;
	size_t definitionCount;
	size_t callSiteCount;
	size_t bodyFrameCount;

	ScopedPureFlexExecution(PureExecutionState &executionState, Section *definitionSection, Section *callSiteSection)
		: state(executionState), definitionCount(state.activeFlexDefinitionStack.size()),
		  callSiteCount(state.flexCallSiteSectionStack.size()), bodyFrameCount(state.sectionFlexBodyFrames.size()) {
		requireCompilerInvariant(definitionSection != nullptr, "pure flex execution requires a definition section");
		state.activeFlexDefinitionStack.push_back(definitionSection);
		if (callSiteSection)
			state.flexCallSiteSectionStack.push_back(callSiteSection);
	}

	~ScopedPureFlexExecution() {
		state.activeFlexDefinitionStack.resize(definitionCount);
		state.flexCallSiteSectionStack.resize(callSiteCount);
		state.sectionFlexBodyFrames.resize(bodyFrameCount);
	}
};

static PureExpressionExecutionResult
executePureSectionFlexCallerBody(Expression *executeBodyExpression, PureExecutionState &state, PureExecutionFrame &frame) {
	Section *callSection =
		executeBodyExpression && executeBodyExpression->range.line ? executeBodyExpression->range.line->section : nullptr;
	requireCompilerInvariant(callSection != nullptr, "pure execute body is missing source section context");
	Section *executionSection = nullptr;
	PureSectionFlexBodyExecutionFrame *targetFrame = resolveSectionFlexBodyFrame(
		state.sectionFlexBodyFrames, callSection, state.flexCallSiteSectionStack, state.activeFlexDefinitionStack,
		&executionSection
	);
	requireCompilerInvariant(targetFrame != nullptr, "pure execute body has no matching section flex body");
	// Inference validates one static transfer site. Re-entering that site in a
	// loop is valid, just as native execution revisits the once-emitted body.
	requireCompilerInvariant(
		targetFrame->bodyExecution == Expression::SectionBodyExecution::Explicit,
		"pure execute body does not own an inferred explicit transfer"
	);
	requireCompilerInvariant(
		targetFrame->bodySection && targetFrame->instantiatedBody,
		"pure execute body found a section flex frame without an inferred caller body"
	);
	Section *bodySection = targetFrame->bodySection;
	InstantiatedSectionBody *instantiatedBody = targetFrame->instantiatedBody;
	BindingFrameStack callerBindings = targetFrame->callerBindings;
	pushSectionFlexCallerVariableBindings(
		callerBindings, targetFrame->definitionSection, targetFrame->definitionBody, executionSection, bodySection
	);
	return executePureSection(state, frame, bodySection, instantiatedBody, nullptr, callerBindings);
}

static PureExpressionExecutionResult executePureFlexCall(
	Expression *expr, PatternDefinition *definition, PureExecutionState &state, PureExecutionFrame &frame,
	const BindingFrameStack &bindingFrameStack
) {
	requireCompilerInvariant(definition && definition->section, "pure flex execution requires a selected definition");
	Section *matchedSection = definition->section;
	requireCompilerInvariant(matchedSection->isFlex, "pure flex execution received a non-flex definition");
	requireCompilerInvariant(
		expr && expr->inferredFlexBody && expr->inferredFlexExpansion,
		"pure flex execution encountered a selected call without its inferred replacement"
	);

	BindingFrameStack expandedBindings = bindingFrameStack;
	pushPatternCallBindingScope(expandedBindings, expr, definition);

	Section *callSiteSection = expr->range.line ? expr->range.line->section : nullptr;
	ScopedPureFlexExecution flexScope(state, matchedSection, callSiteSection);
	ScopedPureFlexLocalValues localScope(frame, expr->inferredFlexBody.get());
	std::optional<size_t> bodyFrameIndex;
	if (matchedSection->type == SectionType::Section) {
		Section *bodySection = expr->range.line ? expr->range.line->sectionOpening : nullptr;
		if (bodySection) {
			requireCompilerInvariant(!state.activeBodies.empty(), "pure section flex call has no active caller body");
			InstantiatedSectionBody *instantiatedBody = state.activeBodies.back()->bodyForChild(bodySection);
			bodyFrameIndex = state.sectionFlexBodyFrames.size();
			state.sectionFlexBodyFrames.push_back(
				{matchedSection, expr->inferredFlexBody.get(), bodySection, instantiatedBody, bindingFrameStack,
				 expr->sectionBodyExecution}
			);
		}
	}

	PureExpressionExecutionResult result{};
	matchedSection->forEachDefinitionBodySection([&](Section *definitionBodySection) {
		InstantiatedSectionBody *definitionBody = definitionBodySection == matchedSection
													  ? expr->inferredFlexBody.get()
													  : expr->inferredFlexBody->bodyForChild(definitionBodySection);
		requireCompilerInvariant(definitionBody != nullptr, "pure flex replacement is missing a definition body");
		result = executePureSection(state, frame, definitionBodySection, definitionBody, nullptr, expandedBindings);
		return !result.returned;
	});

	if (bodyFrameIndex && !result.returned && expr->sectionBodyExecution == Expression::SectionBodyExecution::Implicit) {
		PureSectionFlexBodyExecutionFrame &bodyFrame = state.sectionFlexBodyFrames[*bodyFrameIndex];
		requireCompilerInvariant(
			result.sectionOutcome.kind == Expression::SectionOutcome::Kind::None,
			"pure implicit caller body follows an unexpected control-flow outcome"
		);
		requireCompilerInvariant(bodyFrame.instantiatedBody != nullptr, "pure implicit caller body was not inferred");
		BindingFrameStack callerBindings = bodyFrame.callerBindings;
		Section *executionSection =
			expr->inferredFlexExpansion->range.line ? expr->inferredFlexExpansion->range.line->section : matchedSection;
		pushSectionFlexCallerVariableBindings(
			callerBindings, bodyFrame.definitionSection, bodyFrame.definitionBody, executionSection, bodyFrame.bodySection
		);
		result = executePureSection(state, frame, bodyFrame.bodySection, bodyFrame.instantiatedBody, nullptr, callerBindings);
	}
	if (expr->sectionOutcome.kind == Expression::SectionOutcome::Kind::FunctionReturn)
		requireCompilerInvariant(result.returned, "pure return expansion did not return");
	// A caller-body return exits a loop or conditional before its normal header
	// outcome is produced. Preserve that transfer independently of the header.
	if (result.returned) {
		result.sectionOutcome = {};
	} else if (expr->sectionOutcome.kind == Expression::SectionOutcome::Kind::None) {
		result.sectionOutcome = {};
	} else {
		requireCompilerInvariant(
			result.sectionOutcome.kind == expr->sectionOutcome.kind,
			"pure flex execution produced a different section outcome than inference"
		);
	}
	return result;
}
