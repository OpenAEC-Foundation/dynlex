std::optional<Json> encodePortableConstraintType(const DataType &type) {
	if (type.kind == DataType::Kind::Unresolved || type.kind == DataType::Kind::Class ||
		type.kind == DataType::Kind::Function || type.kind == DataType::Kind::ArgumentPack ||
		type.kind == DataType::Kind::ArgumentReference || type.classDefinition || type.typeExpression ||
		type.functionSignature || type.referencedKind == DataType::Kind::Class ||
		type.referencedKind == DataType::Kind::Function)
		return std::nullopt;
	Json element = nullptr;
	if (type.arrayElementType) {
		auto encoded = encodePortableConstraintType(*type.arrayElementType);
		if (!encoded)
			return std::nullopt;
		element = std::move(*encoded);
	}
	return Json::array(
		{static_cast<int>(type.kind), type.numericSize, type.pointerDepth, static_cast<int>(type.referencedKind),
		 type.arraySize, type.matrixRowCount, std::move(element)}
	);
}

DataType decodePortableConstraintType(const Json &encoded) {
	DataType type;
	type.kind = static_cast<DataType::Kind>(encoded.at(0).get<int>());
	type.numericSize = encoded.at(1).get<int>();
	type.pointerDepth = encoded.at(2).get<int>();
	type.referencedKind = static_cast<DataType::Kind>(encoded.at(3).get<int>());
	type.arraySize = encoded.at(4).get<int>();
	type.matrixRowCount = encoded.at(5).get<int>();
	if (!encoded.at(6).is_null())
		type.arrayElementType = std::make_shared<DataType>(decodePortableConstraintType(encoded.at(6)));
	return type;
}

std::optional<Json> encodePortableConstraint(const TypeConstraint &constraint) {
	if (!constraint.isResolved() || constraint.constrainsClassDefinition || constraint.classDefinition ||
		constraint.classInstantiationIndex || constraint.functionSignature)
		return std::nullopt;
	Json element = nullptr;
	if (constraint.elementConstraint) {
		auto encoded = encodePortableConstraint(*constraint.elementConstraint);
		if (!encoded)
			return std::nullopt;
		element = std::move(*encoded);
	}
	auto optionalInt = [](std::optional<int> value) -> Json {
		return value ? Json(*value) : Json(nullptr);
	};
	return Json::array(
		{constraint.kind ? Json(static_cast<int>(*constraint.kind)) : Json(nullptr), optionalInt(constraint.numericSize),
		 optionalInt(constraint.pointerDepth), optionalInt(constraint.arraySize), optionalInt(constraint.matrixRows),
		 optionalInt(constraint.matrixColumns), constraint.requiresNumeric, constraint.requiresInteger,
		 constraint.requiresCompileTimeValue, std::move(element)}
	);
}

TypeConstraint decodePortableConstraint(const Json &encoded) {
	TypeConstraint constraint = TypeConstraint::any();
	if (!encoded.at(0).is_null())
		constraint.kind = static_cast<DataType::Kind>(encoded.at(0).get<int>());
	auto optionalInt = [](const Json &value) -> std::optional<int> {
		return value.is_null() ? std::nullopt : std::optional<int>{value.get<int>()};
	};
	constraint.numericSize = optionalInt(encoded.at(1));
	constraint.pointerDepth = optionalInt(encoded.at(2));
	constraint.arraySize = optionalInt(encoded.at(3));
	constraint.matrixRows = optionalInt(encoded.at(4));
	constraint.matrixColumns = optionalInt(encoded.at(5));
	constraint.requiresNumeric = encoded.at(6).get<bool>();
	constraint.requiresInteger = encoded.at(7).get<bool>();
	constraint.requiresCompileTimeValue = encoded.at(8).get<bool>();
	if (!encoded.at(9).is_null())
		constraint.elementConstraint = std::make_shared<TypeConstraint>(decodePortableConstraint(encoded.at(9)));
	return constraint;
}

std::optional<Json> encodePortableConstraintValue(const ParseContext::IncrementalConstraintValue &value) {
	auto constraint = encodePortableConstraint(value.constraint);
	auto type = encodePortableConstraintType(value.parameterType);
	if (!constraint || !type)
		return std::nullopt;
	return Json::array({std::move(*constraint), std::move(*type)});
}

ParseContext::IncrementalConstraintValue decodePortableConstraintValue(const Json &encoded) {
	return {decodePortableConstraint(encoded.at(0)), decodePortableConstraintType(encoded.at(1))};
}
