std::optional<Json> encodeInferenceKey(const InstantiationKey &key, const ClassIdentityPaths &classes) {
	Json arguments = Json::array(), constants = Json::array();
	for (const DataType &type : key.argumentTypes) {
		std::set<std::pair<const ClassDefinition *, int>> visiting;
		auto identity = inferenceTypeIdentity(type, classes, visiting);
		if (!identity)
			return std::nullopt;
		arguments.push_back(std::move(*identity));
	}
	for (const auto &[name, value] : key.compileTimeParameters) {
		auto encoded = encodeInferenceValue(value, classes);
		if (!encoded)
			return std::nullopt;
		constants.push_back(Json::array({name, std::move(*encoded)}));
	}
	return Json{{"argument_types", std::move(arguments)}, {"compile_time_parameters", std::move(constants)}};
}

std::optional<InstantiationKey> decodeInferenceKey(const Json &encoded, const ClassIdentityPaths &classes) {
	InstantiationKey key;
	for (const Json &argument : encoded.at("argument_types")) {
		auto type = decodeInferenceTypeIdentity(argument, classes);
		if (!type)
			return std::nullopt;
		key.argumentTypes.push_back(std::move(*type));
	}
	for (const Json &constant : encoded.at("compile_time_parameters")) {
		auto value = decodeInferenceValue(constant.at(1), classes);
		if (!value)
			return std::nullopt;
		key.compileTimeParameters.emplace_back(constant.at(0).get<std::string>(), std::move(*value));
	}
	return key;
}

std::optional<std::string> inferenceSummaryKey(const InstantiationKey &key, const ClassIdentityPaths &classes) {
	auto encoded = encodeInferenceKey(key, classes);
	return encoded ? std::optional<std::string>{digest(encoded->dump())} : std::nullopt;
}
