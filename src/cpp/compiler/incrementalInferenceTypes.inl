using ClassIdentityPaths = std::map<const ClassDefinition *, std::string>;

ClassIdentityPaths classIdentityPaths(const std::map<const Section *, std::string> &sectionPaths) {
	ClassIdentityPaths result;
	for (const auto &[section, path] : sectionPaths) {
		if (section->type == SectionType::Class)
			result.emplace(static_cast<const ClassSection *>(section)->classDefinition, path);
	}
	return result;
}

std::optional<Json> inferenceTypeIdentity(
	const DataType &type, const ClassIdentityPaths &classPaths, std::set<std::pair<const ClassDefinition *, int>> &visiting
) {
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
		auto element = inferenceTypeIdentity(*type.arrayElementType, classPaths, visiting);
		if (!element)
			return std::nullopt;
		result.push_back(type.arraySize);
		result.push_back(type.matrixRowCount);
		result.push_back(std::move(*element));
		break;
	}
	case DataType::Kind::Class: {
		if (!type.classDefinition || type.classInstIndex < 0 ||
			type.classInstIndex >= static_cast<int>(type.classDefinition->instantiations.size()))
			return std::nullopt;
		auto path = classPaths.find(type.classDefinition);
		if (path == classPaths.end())
			return std::nullopt;
		result.push_back(path->second);
		result.push_back(type.classInstIndex);
		auto key = std::make_pair(static_cast<const ClassDefinition *>(type.classDefinition), type.classInstIndex);
		if (!visiting.insert(key).second)
			break;
		const ClassInstantiation &instantiation = type.classDefinition->instantiations[type.classInstIndex];
		if (instantiation.fieldTypes.size() != type.classDefinition->fields.size())
			return std::nullopt;
		Json fields = Json::array();
		for (const DataType &field : instantiation.fieldTypes) {
			auto identity = inferenceTypeIdentity(field, classPaths, visiting);
			if (!identity)
				return std::nullopt;
			fields.push_back(std::move(*identity));
		}
		visiting.erase(key);
		result.push_back(std::move(fields));
		break;
	}
	case DataType::Kind::Function: {
		if (!type.functionSignature)
			return std::nullopt;
		auto returnType = inferenceTypeIdentity(type.functionSignature->result, classPaths, visiting);
		if (!returnType)
			return std::nullopt;
		Json arguments = Json::array();
		for (const DataType &argument : type.functionSignature->parameters) {
			auto identity = inferenceTypeIdentity(argument, classPaths, visiting);
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

std::optional<Json> captureGlobalTypeProjection(const std::map<const Section *, std::string> &sectionPaths) {
	ClassIdentityPaths classPaths = classIdentityPaths(sectionPaths);
	Json result = Json::object();
	for (const auto &[section, path] : sectionPaths) {
		for (const auto &[name, variable] : section->variables) {
			if (!variable->isGlobal)
				continue;
			std::set<std::pair<const ClassDefinition *, int>> visiting;
			auto identity = inferenceTypeIdentity(variable->type, classPaths, visiting);
			if (!identity)
				return std::nullopt;
			result[Json::array({path, name}).dump()] = std::move(*identity);
		}
	}
	return result;
}
