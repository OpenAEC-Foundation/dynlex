std::optional<Json> encodeClassRequestValue(const CompileTimeValue &value, const ClassIdentityPaths &classPaths) {
	if (std::holds_alternative<std::monostate>(value))
		return Json::array({"unknown"});
	if (std::holds_alternative<MinimumSignedIntegerMagnitude>(value))
		return Json::array({"minimum"});
	if (const auto *reference = std::get_if<TypeReferenceValue>(&value)) {
		auto type = encodePortableConstraintType(reference->type, classPaths);
		auto constraint = encodePortableConstraint(reference->constraint, classPaths);
		if (!type || !constraint)
			return std::nullopt;
		return Json::array({"type", std::move(*type), std::move(*constraint)});
	}
	if (const auto *constraint = std::get_if<TypeConstraint>(&value)) {
		auto encoded = encodePortableConstraint(*constraint, classPaths);
		if (!encoded)
			return std::nullopt;
		return Json::array({"constraint", std::move(*encoded)});
	}
	auto scalar = encodeGlobalConstant(value);
	return scalar ? std::optional<Json>{Json::array({"scalar", std::move(*scalar)})} : std::nullopt;
}

CompileTimeValue decodeClassRequestValue(const Json &encoded, const std::map<std::string, Section *> &sections) {
	const std::string kind = encoded.at(0).get<std::string>();
	if (kind == "unknown")
		return {};
	if (kind == "minimum")
		return MinimumSignedIntegerMagnitude{std::make_shared<const MinimumSignedIntegerMagnitudeIdentity>()};
	if (kind == "type")
		return TypeReferenceValue{
			decodePortableConstraintType(encoded.at(1), sections), decodePortableConstraint(encoded.at(2), sections)
		};
	if (kind == "constraint")
		return decodePortableConstraint(encoded.at(1), sections);
	if (kind == "scalar")
		return decodeGlobalConstant(encoded.at(1));
	crashCompilerBug("cached class request contains an unknown value kind");
}

std::optional<Json> encodeClassInstantiationRequests(const ClassDefinition &definition, const ClassIdentityPaths &paths) {
	Json result = Json::array();
	for (const auto &[key, record] : definition.instantiationsByRequest) {
		if (!record.persistent)
			return std::nullopt;
		Json parameters = Json::array();
		for (const auto &[name, type, value] : record.persistent->parameters) {
			auto encodedType = encodePortableConstraintType(type, paths);
			auto encodedValue = encodeClassRequestValue(value, paths);
			if (!encodedType || !encodedValue)
				return std::nullopt;
			parameters.push_back(Json::array({name, std::move(*encodedType), std::move(*encodedValue)}));
		}
		Json construction = nullptr;
		if (record.persistent->constructionArgumentTypes) {
			construction = Json::array();
			for (const DataType &type : *record.persistent->constructionArgumentTypes) {
				auto encoded = encodePortableConstraintType(type, paths);
				if (!encoded)
					return std::nullopt;
				construction.push_back(std::move(*encoded));
			}
		}
		result.push_back(Json::array({record.index, std::move(parameters), std::move(construction)}));
	}
	std::sort(result.begin(), result.end());
	return result;
}

void restoreClassInstantiationRequests(
	ClassDefinition &definition, const Json &requests, const std::map<std::string, Section *> &sections
) {
	requireCompilerInvariant(definition.instantiationsByRequest.empty(), "class requests exist before cache restoration");
	for (const Json &encoded : requests) {
		int index = encoded.at(0).get<int>();
		requireCompilerInvariant(
			index >= 0 && index < static_cast<int>(definition.instantiations.size()), "saved class request has no layout"
		);
		ClassInstantiationRequest request;
		for (const Json &parameter : encoded.at(1))
			request.parameters.emplace_back(
				parameter.at(0).get<std::string>(), decodePortableConstraintType(parameter.at(1), sections),
				decodeClassRequestValue(parameter.at(2), sections)
			);
		if (!encoded.at(2).is_null()) {
			request.constructionArgumentTypes.emplace();
			for (const Json &type : encoded.at(2))
				request.constructionArgumentTypes->push_back(decodePortableConstraintType(type, sections));
		}
		std::string key = classInstantiationRequestKey(request);
		auto [entry, inserted] = definition.instantiationsByRequest.emplace(
			std::move(key),
			ClassInstantiationRequestRecord{index, std::make_shared<const ClassInstantiationRequest>(std::move(request))}
		);
		requireCompilerInvariant(inserted, "saved class requests have duplicate identities");
	}
}
