ClassIdentityPaths classIdentityPaths(const std::map<const Section *, std::string> &sectionPaths) {
	ClassIdentityPaths result;
	for (const auto &[section, path] : sectionPaths) {
		if (section->type == SectionType::Class)
			result.emplace(static_cast<const ClassSection *>(section)->classDefinition, path);
	}
	return result;
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
	auto current = typeIdentity(*type, classPaths);
	return current && *current == encoded ? std::move(type) : std::nullopt;
}

std::optional<Json> captureGlobalTypeProjection(const std::map<const Section *, std::string> &sectionPaths) {
	ClassIdentityPaths classPaths = classIdentityPaths(sectionPaths);
	Json result = Json::object();
	for (const auto &[section, path] : sectionPaths) {
		for (const auto &[name, variable] : section->variables) {
			if (!variable->isGlobal)
				continue;
			auto identity = typeIdentity(variable->type, classPaths);
			if (!identity)
				return std::nullopt;
			result[Json::array({path, name}).dump()] = std::move(*identity);
		}
	}
	return result;
}
