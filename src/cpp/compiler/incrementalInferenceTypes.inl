using ClassIdentityPaths = std::map<const ClassDefinition *, std::string>;

ClassIdentityPaths classIdentityPaths(const std::map<const Section *, std::string> &sectionPaths) {
	ClassIdentityPaths result;
	for (const auto &[section, path] : sectionPaths) {
		if (section->type == SectionType::Class)
			result.emplace(static_cast<const ClassSection *>(section)->classDefinition, path);
	}
	return result;
}

class InferenceTypeEncoder {
	using ClassInstance = std::pair<const ClassDefinition *, int>;
	const ClassIdentityPaths &classPaths;
	std::set<ClassInstance> seen;
	std::vector<ClassInstance> pending;

	std::optional<Json> encodeShape(const DataType &type);

  public:
	explicit InferenceTypeEncoder(const ClassIdentityPaths &classPaths) : classPaths(classPaths) {}

	std::optional<Json> encode(const DataType &type) {
		auto shape = encodeShape(type);
		if (!shape)
			return std::nullopt;
		Json layouts = Json::array();
		// Visit each reachable instantiation once. Fields add references to the
		// worklist in order, including when a class layout is recursive.
		for (size_t i = 0; i < pending.size(); i++) {
			const auto [definition, index] = pending[i];
			const auto &instantiation = definition->instantiations[index];
			if (instantiation.fieldTypes.size() != definition->fields.size())
				return std::nullopt;
			Json fields = Json::array();
			for (const DataType &field : instantiation.fieldTypes) {
				auto identity = encodeShape(field);
				if (!identity)
					return std::nullopt;
				fields.push_back(std::move(*identity));
			}
			layouts.push_back(Json::array({classPaths.at(definition), index, std::move(fields)}));
		}
		Json layoutDigest = layouts.empty() ? Json(nullptr) : Json(digest(layouts.dump()));
		return Json::array({std::move(*shape), std::move(layoutDigest)});
	}
};

std::optional<Json> InferenceTypeEncoder::encodeShape(const DataType &type) {
	if (!type.isConcrete())
		return std::nullopt;
	Json result = Json::array({static_cast<int>(type.kind), type.numericSize, type.pointerDepth});
	switch (type.kind) {
	case DataType::Kind::Void:
	case DataType::Kind::Bool:
	case DataType::Kind::Float:
	case DataType::Kind::Int:
	case DataType::Kind::UInt:
		break;
	case DataType::Kind::Array:
	case DataType::Kind::Vector:
	case DataType::Kind::Matrix: {
		if (!type.arrayElementType)
			return std::nullopt;
		auto element = encodeShape(*type.arrayElementType);
		if (!element)
			return std::nullopt;
		result.push_back(type.arraySize);
		result.push_back(type.matrixRowCount);
		result.push_back(std::move(*element));
		break;
	}
	case DataType::Kind::Class: {
		if (!type.classDefinition || (type.classInstIndex < 0 && type.pointerDepth == 0) ||
			type.classInstIndex >= static_cast<int>(type.classDefinition->instantiations.size()))
			return std::nullopt;
		auto path = classPaths.find(type.classDefinition);
		if (path == classPaths.end())
			return std::nullopt;
		result.push_back(path->second);
		result.push_back(type.classInstIndex);
		ClassInstance key{type.classDefinition, type.classInstIndex};
		if (type.classInstIndex >= 0 && seen.insert(key).second)
			pending.push_back(key);
		break;
	}
	case DataType::Kind::Function: {
		if (!type.functionSignature)
			return std::nullopt;
		auto returnType = encodeShape(type.functionSignature->result);
		if (!returnType)
			return std::nullopt;
		Json arguments = Json::array();
		for (const DataType &argument : type.functionSignature->parameters) {
			auto identity = encodeShape(argument);
			if (!identity)
				return std::nullopt;
			arguments.push_back(std::move(*identity));
		}
		result.push_back(std::move(*returnType));
		result.push_back(std::move(arguments));
		break;
	}
	default:
		return std::nullopt;
	}
	return result;
}

std::optional<Json> inferenceTypeIdentity(const DataType &type, const ClassIdentityPaths &classPaths) {
	return InferenceTypeEncoder(classPaths).encode(type);
}

std::optional<DataType> decodeInferenceTypeShape(const Json &encoded, const ClassIdentityPaths &classPaths) {
	DataType result{static_cast<DataType::Kind>(encoded.at(0).get<int>()), encoded.at(1).get<int>(), encoded.at(2).get<int>()};
	switch (result.kind) {
	case DataType::Kind::Void:
	case DataType::Kind::Bool:
	case DataType::Kind::Float:
	case DataType::Kind::Int:
	case DataType::Kind::UInt:
		break;
	case DataType::Kind::Array:
	case DataType::Kind::Vector:
	case DataType::Kind::Matrix: {
		result.arraySize = encoded.at(3).get<int>();
		result.matrixRowCount = encoded.at(4).get<int>();
		auto element = decodeInferenceTypeShape(encoded.at(5), classPaths);
		if (!element)
			return std::nullopt;
		result.arrayElementType = std::make_shared<DataType>(std::move(*element));
		break;
	}
	case DataType::Kind::Class: {
		const std::string path = encoded.at(3).get<std::string>();
		for (const auto &[definition, candidate] : classPaths)
			if (candidate == path) {
				result.classDefinition = const_cast<ClassDefinition *>(definition);
				break;
			}
		if (!result.classDefinition)
			return std::nullopt;
		result.classInstIndex = encoded.at(4).get<int>();
		break;
	}
	case DataType::Kind::Function: {
		auto returnType = decodeInferenceTypeShape(encoded.at(3), classPaths);
		if (!returnType)
			return std::nullopt;
		std::vector<DataType> parameters;
		for (const Json &argument : encoded.at(4)) {
			auto parameter = decodeInferenceTypeShape(argument, classPaths);
			if (!parameter)
				return std::nullopt;
			parameters.push_back(std::move(*parameter));
		}
		result.functionSignature = std::make_shared<FunctionSignature>(std::move(*returnType), std::move(parameters));
		break;
	}
	default:
		return std::nullopt;
	}
	return result;
}

std::optional<DataType> decodeInferenceTypeIdentity(const Json &encoded, const ClassIdentityPaths &classPaths) {
	auto type = decodeInferenceTypeShape(encoded.at(0), classPaths);
	if (!type)
		return std::nullopt;
	auto current = inferenceTypeIdentity(*type, classPaths);
	return current && *current == encoded ? std::move(type) : std::nullopt;
}

std::optional<Json> captureGlobalTypeProjection(const std::map<const Section *, std::string> &sectionPaths) {
	ClassIdentityPaths classPaths = classIdentityPaths(sectionPaths);
	Json result = Json::object();
	for (const auto &[section, path] : sectionPaths) {
		for (const auto &[name, variable] : section->variables) {
			if (!variable->isGlobal)
				continue;
			auto identity = inferenceTypeIdentity(variable->type, classPaths);
			if (!identity)
				return std::nullopt;
			result[Json::array({path, name}).dump()] = std::move(*identity);
		}
	}
	return result;
}
