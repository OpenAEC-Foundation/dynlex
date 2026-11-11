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

template <typename Remap> Json remapInferenceSummaryTypes(const Json &sections, const Remap &remap) {
	Json result = Json::object();
	for (const auto &[path, variants] : sections.items()) {
		Json mapped = Json::object();
		for (const auto &[key, summary] : variants.items()) {
			Json record = Json::object();
			for (const auto &[field, value] : summary.items()) {
				if (field == "argument_types") {
					Json arguments = Json::array();
					for (const Json &argument : value)
						arguments.push_back(remap(argument));
					record[field] = std::move(arguments);
				} else if (field == "return_type")
					record[field] = remap(value);
				else
					record[field] = value;
			}
			mapped[key] = std::move(record);
		}
		result[path] = std::move(mapped);
	}
	return result;
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
		// Loaded JSON uses unsigned values for positive integers. Fresh
		// snapshots can use signed values for the same type metadata.
		return llvm::hash_combine('i', value->get<int64_t>());
	}
};

struct InferenceTypeEqual {
	bool operator()(const Json *left, const Json *right) const { return *left == *right; }
};

Json encodeInferenceSummaries(const Json &summaries) {
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
	Json sections = remapInferenceSummaryTypes(summaries, intern);
	return Json{{"types", std::move(types)}, {"sections", std::move(sections)}};
}

Json decodeInferenceSummaries(const Json &encoded) {
	Json types = Json::array();
	auto expand = [&](const Json &index) -> const Json & {
		return types.at(index.get<size_t>());
	};
	for (const Json &node : encoded.at("types"))
		types.push_back(remapInferenceTypeChildren(node, expand));
	return remapInferenceSummaryTypes(encoded.at("sections"), expand);
}
