struct InferenceParameterState {
	std::unordered_map<std::string, DataType> types, outputs;
	std::unordered_map<std::string, CompileTimeValue> constants;
	std::unordered_set<std::string> requiredCompileTime;
};

std::optional<Json> encodeInferenceParameters(const Instantiation &instantiation, const ClassIdentityPaths &classes) {
	requireCompilerInvariant(instantiation.parameterSeedTypesByName.empty(), "completed inference retained parameter seeds");
	Json types = Json::object(), outputs = Json::object(), constants = Json::object();
	auto capture = [&](const auto &source, Json &destination) {
		for (const auto &[name, type] : source) {
			std::set<std::pair<const ClassDefinition *, int>> visiting;
			auto identity = inferenceTypeIdentity(type, classes, visiting);
			if (!identity)
				return false;
			destination[name] = std::move(*identity);
		}
		return true;
	};
	if (!capture(instantiation.parameterTypesByName, types) || !capture(instantiation.parameterOutputTypesByName, outputs))
		return std::nullopt;
	for (const auto &[name, value] : instantiation.constantParameterValues) {
		auto encoded = encodeInferenceValue(value, classes);
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

std::optional<InferenceParameterState> decodeInferenceParameters(const Json &encoded, const ClassIdentityPaths &classes) {
	InferenceParameterState result;
	auto restore = [&](const Json &source, auto &destination) {
		for (const auto &[name, identity] : source.items()) {
			auto type = decodeInferenceTypeIdentity(identity, classes);
			if (!type)
				return false;
			destination.emplace(name, std::move(*type));
		}
		return true;
	};
	if (!restore(encoded.at("types"), result.types) || !restore(encoded.at("outputs"), result.outputs))
		return std::nullopt;
	for (const auto &[name, encodedValue] : encoded.at("constants").items()) {
		auto value = decodeInferenceValue(encodedValue, classes);
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
