bool classRequestTypeIsPortable(const DataType &type) {
	if (type.typeExpression)
		return false;
	if (type.arrayElementType && !classRequestTypeIsPortable(*type.arrayElementType))
		return false;
	if (type.functionSignature) {
		if (!classRequestTypeIsPortable(type.functionSignature->result))
			return false;
		for (const DataType &parameter : type.functionSignature->parameters)
			if (!classRequestTypeIsPortable(parameter))
				return false;
	}
	return true;
}

Json encodeClassRequests(const ClassDefinition &definition, const InferenceValueIdentities &identities) {
	std::vector<Json> requests;
	for (const auto &[key, pointer] : definition.instantiationRequests) {
		const auto &request = *pointer;
		Json parameters = Json::array(), construction = nullptr;
		for (const auto &[name, type, value] : request.parameters) {
			if (!classRequestTypeIsPortable(type))
				return nullptr;
			if (const auto *reference = std::get_if<TypeReferenceValue>(&value);
				reference && !classRequestTypeIsPortable(reference->type))
				return nullptr;
			auto encodedType = encodePortableConstraintType(type, identities.classes);
			auto encodedValue = encodeInferenceValue(value, identities);
			if (!encodedType || !encodedValue)
				return nullptr;
			parameters.push_back(Json::array({name, std::move(*encodedType), std::move(*encodedValue)}));
		}
		if (request.constructionArgumentTypes) {
			construction = Json::array();
			for (const DataType &type : *request.constructionArgumentTypes) {
				if (!classRequestTypeIsPortable(type))
					return nullptr;
				auto encoded = encodePortableConstraintType(type, identities.classes);
				if (!encoded)
					return nullptr;
				construction.push_back(std::move(*encoded));
			}
		}
		requests.push_back(Json::array({std::move(parameters), std::move(construction), request.index}));
	}
	std::ranges::sort(requests);
	return requests;
}

std::optional<std::map<std::string, std::shared_ptr<const ClassInstantiationRequest>>>
decodeClassRequests(const Json &records, const InferenceValueIdentities &identities) {
	if (records.is_null())
		return std::nullopt;
	std::map<std::string, std::shared_ptr<const ClassInstantiationRequest>> result;
	for (const Json &record : records) {
		ClassInstantiationRequest request;
		for (const Json &parameter : record.at(0)) {
			if (!portableConstraintTypeReferencesPresent(parameter.at(1), identities.classes))
				return std::nullopt;
			auto value = decodeInferenceValue(parameter.at(2), identities);
			if (!value)
				return std::nullopt;
			request.parameters.emplace_back(
				parameter.at(0).get<std::string>(), decodePortableConstraintType(parameter.at(1), identities.classes),
				std::move(*value)
			);
		}
		if (!record.at(1).is_null()) {
			request.constructionArgumentTypes.emplace();
			for (const Json &type : record.at(1)) {
				if (!portableConstraintTypeReferencesPresent(type, identities.classes))
					return std::nullopt;
				request.constructionArgumentTypes->push_back(decodePortableConstraintType(type, identities.classes));
			}
		}
		request.index = record.at(2).get<int>();
		std::string key = classInstantiationRequestKey(request);
		auto [found, inserted] =
			result.emplace(std::move(key), std::make_shared<const ClassInstantiationRequest>(std::move(request)));
		requireCompilerInvariant(inserted, "saved class requests have duplicate inputs");
	}
	return result;
}
