#pragma once

static AddressInferenceState mergeAddressInferenceStates(const std::vector<AddressInferenceState> &states);

// Inferring an argument determines its type even when the selected replacement
// does not execute it. Restore only execution effects; keep inferred types,
// overload selections and recursive inference dependencies.
struct InferenceExecutionSnapshot {
	KnownConstantState values;
	AddressInferenceState addresses;
	InferenceContext::SubjectState subject;
	TypeRefinementState types;
	Instantiation *instantiation;
	std::optional<InstantiationExecutionEffects> effects;

	explicit InferenceExecutionSnapshot(const InferenceContext &context)
		: values(context.currentVariableValues), addresses(context.currentAddressState), subject(context.currentSubject),
		  types(context.typeRefinements), instantiation(context.currentInstantiation) {
		if (instantiation)
			effects = *instantiation;
	}

	void restore(InferenceContext &context) const {
		requireCompilerInvariant(context.currentInstantiation == instantiation, "argument inference changed its owner");
		restoreState(context);
		if (effects) {
			if (context.trial)
				context.trialJournal->recordInstantiationWrite(instantiation);
			static_cast<InstantiationExecutionEffects &>(*instantiation) = *effects;
		}
	}

	void restoreState(InferenceContext &context) const {
		context.currentVariableValues = values;
		context.currentAddressState = addresses;
		context.currentSubject = subject;
	}

	bool matches(const InferenceContext &context) const {
		return context.currentInstantiation == instantiation && context.currentVariableValues == values &&
			   context.currentAddressState == addresses && context.currentSubject == subject &&
			   types.sharesStorageWith(context.typeRefinements) &&
			   (!effects || *effects == static_cast<const InstantiationExecutionEffects &>(*instantiation));
	}
};

static void mergeExecutionStates(
	InferenceContext &context, const std::vector<KnownConstantState> &constantStates,
	const std::vector<AddressInferenceState> &addressStates, const std::vector<InferenceContext::SubjectState> &subjectStates
) {
	requireCompilerInvariant(!constantStates.empty(), "execution state merge requires a reachable path");
	requireCompilerInvariant(
		constantStates.size() == addressStates.size() && constantStates.size() == subjectStates.size(),
		"execution state counts diverged"
	);
	KnownConstantStorage mergedConstants;
	for (const auto &state : constantStates) {
		for (const auto &[reference, value] : state.read()) {
			(void)value;
			mergedConstants.try_emplace(reference, CompileTimeValue{});
		}
	}
	for (auto &[reference, mergedValue] : mergedConstants) {
		const KnownConstantStorage &firstState = constantStates.front().read();
		auto first = firstState.find(reference);
		if (first == firstState.end())
			continue;
		bool sameValueOnEveryPath = std::ranges::all_of(constantStates, [&](const auto &state) {
			const KnownConstantStorage &values = state.read();
			auto value = values.find(reference);
			return value != values.end() && value->second == first->second;
		});
		if (sameValueOnEveryPath)
			mergedValue = first->second;
	}
	context.currentVariableValues = KnownConstantState(std::move(mergedConstants));
	context.currentAddressState = mergeAddressInferenceStates(addressStates);
	context.currentSubject = subjectStates.front();
	for (size_t index = 1; index < subjectStates.size(); index++) {
		if (subjectStates[index] != context.currentSubject) {
			context.currentSubject = {.setter = nullptr, .ambiguous = true};
			break;
		}
	}
}
