using SymbolicInferenceUnits = std::set<std::pair<std::string, std::string>>;

void collectSymbolicInferenceValue(const Json &encoded, SymbolicInferenceUnits &targets) {
	if (encoded.is_null() || encoded.at(0) != "function")
		return;
	Json key{{"argument_types", encoded.at(4)}, {"compile_time_parameters", Json::array()}};
	targets.emplace(encoded.at(1).get<std::string>(), digest(key.dump()));
}

Json captureSymbolicInferenceDependencies(const Json &summary) {
	SymbolicInferenceUnits targets;
	auto value = [&](const Json &encoded) {
		collectSymbolicInferenceValue(encoded, targets);
	};
	auto constants = [&](const Json &entries) {
		for (const Json &entry : entries)
			value(entry.at(1));
	};
	std::function<void(const Json &)> scope = [&](const Json &encoded) {
		if (encoded.is_null())
			return;
		if (encoded.contains("parent"))
			scope(encoded.at("parent"));
		else
			constants(encoded.at("compile_time_parameters"));
	};
	auto provenance = [&](const Json &encoded) {
		for (const Json &target : encoded.at("targets"))
			scope(target.at(1));
	};
	constants(summary.at("compile_time_parameters"));
	for (const Json &constant : summary.at("parameters").at("constants"))
		value(constant);
	for (const Json &observation : summary.at("pure_values")) {
		for (const Json &argument : observation.at(0))
			value(argument);
		value(observation.at(1));
	}
	provenance(summary.at("return_provenance"));
	const Json &effects = summary.at("global_effects");
	for (const Json &constant : effects.at("constants"))
		value(constant);
	for (const Json &write : effects.at("writes"))
		provenance(write);
	provenance(effects.at("escaped"));
	return targets;
}

SymbolicInferenceUnits referencedInferenceFunctions(const Json &summaries) {
	SymbolicInferenceUnits result;
	for (const Json &variants : summaries)
		for (const Json &reference : variants) {
			for (const Json &target : reference.at("symbolic_dependencies"))
				result.emplace(target.at(0).get<std::string>(), target.at(1).get<std::string>());
			if (!reference.at("body").is_null())
				for (const Json &target : reference.at("body").at("dependencies"))
					result.emplace(target.at(0).get<std::string>(), target.at(1).get<std::string>());
		}
	return result;
}
