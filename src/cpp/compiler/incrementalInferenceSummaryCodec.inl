// Type children become table indices on disk. Constructing a node from its
// scalar fields avoids copying an expanded class layout while encoding it.
template <typename Remap> Json remapInferenceTypeChildren(const Json &type, const Remap &remap) {
	Json result = Json::array({type.at(0), type.at(1), type.at(2)});
	auto children = [&](const Json &types) {
		Json mapped = Json::array();
		for (const Json &child : types)
			mapped.push_back(remap(child));
		return mapped;
	};
	switch (static_cast<DataType::Kind>(type.at(0).get<int>())) {
	case DataType::Kind::Void:
	case DataType::Kind::Bool:
	case DataType::Kind::Float:
	case DataType::Kind::Int:
	case DataType::Kind::UInt:
	case DataType::Kind::Constraint:
	case DataType::Kind::ArgumentPack:
	case DataType::Kind::ArgumentReference:
		break;
	case DataType::Kind::Type:
		result.push_back(type.at(3));
		result.push_back(type.at(4).is_null() ? Json(nullptr) : Json(remap(type.at(4))));
		break;
	case DataType::Kind::Array:
	case DataType::Kind::Vector:
	case DataType::Kind::Matrix:
		result.push_back(type.at(3));
		result.push_back(type.at(4));
		result.push_back(remap(type.at(5)));
		break;
	case DataType::Kind::Class:
		result.push_back(type.at(3));
		result.push_back(type.at(4));
		if (type.size() == 6)
			result.push_back(children(type.at(5)));
		break;
	case DataType::Kind::Function:
		result.push_back(remap(type.at(3)));
		result.push_back(children(type.at(4)));
		break;
	default:
		crashCompilerBug("unsupported inference type identity in program cache");
	}
	return result;
}

template <typename Remap> Json remapInferenceValue(const Json &value, const Remap &remap) {
	auto constraint = [&](const Json &identity) {
		Json layouts = Json::array();
		for (const Json &layout : identity.at(1))
			layouts.push_back(remap(layout));
		return Json::array({identity.at(0), std::move(layouts)});
	};
	if (value.at(0) == "type")
		return Json::array({value.at(0), remap(value.at(1)), constraint(value.at(2))});
	if (value.at(0) == "constraint")
		return Json::array({value.at(0), constraint(value.at(1))});
	if (value.at(0) == "function") {
		Json arguments = Json::array();
		for (const Json &type : value.at(4))
			arguments.push_back(remap(type));
		return Json::array({value.at(0), value.at(1), value.at(2), value.at(3), std::move(arguments), remap(value.at(5))});
	}
	return value;
}

template <typename Remap> Json remapCompileTimeParameters(const Json &values, const Remap &remap) {
	Json result = Json::array();
	for (const Json &entry : values)
		result.push_back(Json::array({entry.at(0), remapInferenceValue(entry.at(1), remap)}));
	return result;
}

template <typename Remap> Json remapInferenceParameters(const Json &parameters, const Remap &remap) {
	Json result = Json::object();
	for (const char *field : {"types", "outputs", "constants"}) {
		Json values = Json::object();
		for (const auto &[name, value] : parameters.at(field).items())
			values[name] = std::string_view(field) == "constants" ? remapInferenceValue(value, remap) : Json(remap(value));
		result[field] = std::move(values);
	}
	result["required_compile_time"] = parameters.at("required_compile_time");
	return result;
}

template <typename Remap> Json remapInferenceStorageScope(const Json &scope, const Remap &remap) {
	if (scope.is_null())
		return nullptr;
	if (scope.contains("parent"))
		return Json{
			{"parent", remapInferenceStorageScope(scope.at("parent"), remap)},
			{"site", scope.at("site")},
			{"iteration", scope.at("iteration")}
		};
	Json arguments = Json::array();
	for (const Json &type : scope.at("argument_types"))
		arguments.push_back(remap(type));
	return Json{
		{"owner", scope.at("owner")},
		{"compile_time_parameters", remapCompileTimeParameters(scope.at("compile_time_parameters"), remap)},
		{"argument_types", std::move(arguments)}
	};
}

template <typename Remap> Json remapInferenceProvenance(const Json &provenance, const Remap &remap) {
	Json targets = Json::array();
	for (const Json &target : provenance.at("targets"))
		targets.push_back(Json::array({target.at(0), remapInferenceStorageScope(target.at(1), remap)}));
	return Json{
		{"targets", std::move(targets)},
		{"null", provenance.at("null")},
		{"unknown", provenance.at("unknown")},
		{"static", provenance.at("static")}
	};
}

template <typename Remap> Json remapInferenceSummaryTypes(const Json &summary, const Remap &remap) {
	Json record = Json::object();
	for (const auto &[field, value] : summary.items()) {
		if (field == "argument_types") {
			Json arguments = Json::array();
			for (const Json &argument : value)
				arguments.push_back(remap(argument));
			record[field] = std::move(arguments);
		} else if (field == "compile_time_parameters")
			record[field] = remapCompileTimeParameters(value, remap);
		else if (field == "parameters")
			record[field] = remapInferenceParameters(value, remap);
		else if (field == "pure_values") {
			Json observations = Json::array();
			for (const Json &entry : value) {
				Json arguments = Json::array();
				for (const Json &argument : entry.at(0))
					arguments.push_back(remapInferenceValue(argument, remap));
				observations.push_back(Json::array({std::move(arguments), remapInferenceValue(entry.at(1), remap)}));
			}
			record[field] = std::move(observations);
		} else if (field == "return_type")
			record[field] = remap(value);
		else if (field == "return_provenance")
			record[field] = remapInferenceProvenance(value, remap);
		else if (field == "global_effects") {
			Json effects = Json::object();
			for (const auto &[name, provenance] : value.at("writes").items())
				effects[name] = remapInferenceProvenance(provenance, remap);
			Json constants = Json::object();
			for (const auto &[name, constant] : value.at("constants").items())
				constants[name] = remapInferenceValue(constant, remap);
			record[field] = Json{
				{"accessed", value.at("accessed")},
				{"writes", std::move(effects)},
				{"constants", std::move(constants)},
				{"taken", value.at("taken")},
				{"escaped", remapInferenceProvenance(value.at("escaped"), remap)}
			};
		} else
			record[field] = value;
	}
	return record;
}

struct InferenceTypeHash {
	size_t operator()(const Json *value) const {
		if (value->is_array()) {
			size_t hash = llvm::hash_combine('a', value->size());
			for (const Json &element : *value)
				hash = llvm::hash_combine(hash, (*this)(&element));
			return hash;
		}
		if (value->is_string())
			return llvm::hash_combine('s', llvm::StringRef(value->get_ref<const std::string &>()));
		if (value->is_null())
			return llvm::hash_combine('n');
		if (value->is_boolean())
			return llvm::hash_combine('b', value->get<bool>());
		// Loaded JSON uses unsigned values for positive integers. Fresh
		// snapshots can use signed values for the same type metadata.
		return llvm::hash_combine('i', value->get<int64_t>());
	}
};

struct InferenceTypeEqual {
	bool operator()(const Json *left, const Json *right) const { return *left == *right; }
};

Json encodeInferenceSummary(const Json &summary) {
	Json types = Json::array();
	// The input remains immutable throughout encoding. A repeated complete
	// type can be reused before visiting or serializing any of its children.
	std::unordered_map<const Json *, size_t, InferenceTypeHash, InferenceTypeEqual> indices;
	std::function<size_t(const Json &)> intern = [&](const Json &type) {
		if (auto found = indices.find(&type); found != indices.end())
			return found->second;
		Json node = remapInferenceTypeChildren(type, intern);
		size_t index = types.size();
		types.push_back(std::move(node));
		indices.emplace(&type, index);
		return index;
	};
	Json record = remapInferenceSummaryTypes(summary, intern);
	return Json{{"types", std::move(types)}, {"summary", std::move(record)}};
}

Json decodeInferenceSummary(const Json &encoded) {
	Json types = Json::array();
	auto expand = [&](const Json &index) -> const Json & {
		return types.at(index.get<size_t>());
	};
	for (const Json &node : encoded.at("types"))
		types.push_back(remapInferenceTypeChildren(node, expand));
	return remapInferenceSummaryTypes(encoded.at("summary"), expand);
}
