// Class units use the portable field-type schema, including arrays and
// callable signatures. References are independent of compiler addresses.
void collectPortableClassDependencies(const Json &type, std::set<std::string> &result) {
	if (!type.at(7).is_null() && type.at(8).get<int>() >= 0)
		result.insert(type.at(7).get<std::string>());
	if (!type.at(6).is_null())
		collectPortableClassDependencies(type.at(6), result);
	if (!type.at(9).is_null()) {
		collectPortableClassDependencies(type.at(9).at(0), result);
		for (const Json &parameter : type.at(9).at(1))
			collectPortableClassDependencies(parameter, result);
	}
}

void collectInferenceClassDependencies(const Json &type, std::set<std::string> &result) {
	switch (static_cast<DataType::Kind>(type.at(0).get<int>())) {
	case DataType::Kind::Class:
		if (type.at(4).get<int>() >= 0)
			result.insert(type.at(3).get<std::string>());
		if (type.size() == 6)
			for (const Json &field : type.at(5))
				collectInferenceClassDependencies(field, result);
		break;
	case DataType::Kind::Type:
		collectPortableClassDependencies(type.at(3), result);
		if (!type.at(4).is_null())
			collectInferenceClassDependencies(type.at(4), result);
		break;
	case DataType::Kind::Array:
	case DataType::Kind::Vector:
	case DataType::Kind::Matrix:
		collectInferenceClassDependencies(type.at(5), result);
		break;
	case DataType::Kind::Function:
		collectInferenceClassDependencies(type.at(3), result);
		for (const Json &parameter : type.at(4))
			collectInferenceClassDependencies(parameter, result);
		break;
	default:
		break;
	}
}

void collectValueClassDependencies(const Json &value, std::set<std::string> &result) {
	if (value.is_null())
		return;
	if (value.at(0) == "type") {
		collectInferenceClassDependencies(value.at(1), result);
		for (const Json &layout : value.at(2).at(1))
			collectInferenceClassDependencies(layout, result);
	} else if (value.at(0) == "constraint") {
		for (const Json &layout : value.at(1).at(1))
			collectInferenceClassDependencies(layout, result);
	} else if (value.at(0) == "function") {
		for (const Json &parameter : value.at(4))
			collectInferenceClassDependencies(parameter, result);
		collectInferenceClassDependencies(value.at(5), result);
	}
}

std::set<std::string> classInstantiationDependencies(const Json &state) {
	std::set<std::string> dependencies;
	for (const Json &instance : state.at("instances"))
		for (const Json &field : instance)
			collectPortableClassDependencies(field, dependencies);
	if (!state.at("requests").is_null())
		for (const Json &request : state.at("requests")) {
			for (const Json &parameter : request.at(0)) {
				collectPortableClassDependencies(parameter.at(1), dependencies);
				collectValueClassDependencies(parameter.at(2), dependencies);
			}
			if (!request.at(1).is_null())
				for (const Json &type : request.at(1))
					collectPortableClassDependencies(type, dependencies);
		}
	return dependencies;
}
