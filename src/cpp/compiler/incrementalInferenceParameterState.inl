struct InferenceInvocation {
	PatternDefinition *definition;
	std::vector<std::string> names;
	std::unordered_set<std::string> requiredCompileTime;
	std::string declaration;
};

Json encodeInferenceInvocation(const Section &section, const Instantiation &instance, const std::string &declaration) {
	requireCompilerInvariant(
		instance.argumentNames.size() == instance.argumentTypes.size(), "inference invocation lost its argument names"
	);
	Json definition = nullptr;
	if (instance.invokedDefinition) {
		auto found = std::ranges::find(section.patternDefinitions, instance.invokedDefinition);
		requireCompilerInvariant(found != section.patternDefinitions.end(), "inference invocation has a foreign definition");
		definition = std::distance(section.patternDefinitions.begin(), found);
	}
	return Json::array(
		{std::move(definition), instance.argumentNames,
		 std::set<std::string>(instance.requiredCompileTimeParameters.begin(), instance.requiredCompileTimeParameters.end()),
		 declaration}
	);
}

InferenceInvocation decodeInferenceInvocation(Section &section, const Json &encoded) {
	return {
		encoded.at(0).is_null() ? nullptr : section.patternDefinitions.at(encoded.at(0).get<size_t>()),
		encoded.at(1).get<std::vector<std::string>>(), encoded.at(2).get<std::unordered_set<std::string>>(),
		encoded.at(3).get<std::string>()
	};
}

struct InferenceParameterState {
	std::unordered_map<std::string, DataType> types, outputs;
	std::unordered_map<std::string, CompileTimeValue> constants;
	std::unordered_set<std::string> requiredCompileTime;
};

std::optional<Json> encodeInferenceParameters(const Instantiation &instantiation, const InferenceValueIdentities &identities) {
	requireCompilerInvariant(instantiation.parameterSeedTypesByName.empty(), "completed inference retained parameter seeds");
	Json types = Json::object(), outputs = Json::object(), constants = Json::object();
	auto capture = [&](const auto &source, Json &destination) {
		for (const auto &[name, type] : source) {
			std::set<std::pair<const ClassDefinition *, int>> visiting;
			auto identity = inferenceTypeIdentity(type, identities.classes, visiting);
			if (!identity)
				return false;
			destination[name] = std::move(*identity);
		}
		return true;
	};
	if (!capture(instantiation.parameterTypesByName, types) || !capture(instantiation.parameterOutputTypesByName, outputs))
		return std::nullopt;
	for (const auto &[name, value] : instantiation.constantParameterValues) {
		auto encoded = encodeInferenceValue(value, identities);
		if (!encoded)
			return std::nullopt;
		constants[name] = std::move(*encoded);
	}
	std::set<std::string> required(
		instantiation.requiredCompileTimeParameters.begin(), instantiation.requiredCompileTimeParameters.end()
	);
	return Json{
		{"types", std::move(types)},
		{"outputs", std::move(outputs)},
		{"constants", std::move(constants)},
		{"required_compile_time", std::move(required)}
	};
}

std::optional<InferenceParameterState>
decodeInferenceParameters(const Json &encoded, const InferenceValueIdentities &identities) {
	InferenceParameterState result;
	auto restore = [&](const Json &source, auto &destination) {
		for (const auto &[name, identity] : source.items()) {
			auto type = decodeInferenceTypeIdentity(identity, identities.classes);
			if (!type)
				return false;
			destination.emplace(name, std::move(*type));
		}
		return true;
	};
	if (!restore(encoded.at("types"), result.types) || !restore(encoded.at("outputs"), result.outputs))
		return std::nullopt;
	for (const auto &[name, encodedValue] : encoded.at("constants").items()) {
		auto value = decodeInferenceValue(encodedValue, identities);
		if (!value)
			return std::nullopt;
		result.constants.emplace(name, std::move(*value));
	}
	for (const Json &name : encoded.at("required_compile_time"))
		result.requiredCompileTime.insert(name.get<std::string>());
	return result;
}

void applyInferenceParameters(InferenceParameterState state, Instantiation &instantiation) {
	instantiation.parameterTypesByName = std::move(state.types);
	instantiation.parameterOutputTypesByName = std::move(state.outputs);
	instantiation.constantParameterValues = std::move(state.constants);
	instantiation.requiredCompileTimeParameters = std::move(state.requiredCompileTime);
}
