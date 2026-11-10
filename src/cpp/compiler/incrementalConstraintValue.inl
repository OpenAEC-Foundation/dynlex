ClassDefinition *constraintClassForPath(const Json &path, const std::map<std::string, Section *> &sections) {
	Section *section = sections.at(path.get<std::string>());
	requireCompilerInvariant(section->type == SectionType::Class, "saved constraint class path is not a class");
	return static_cast<ClassSection *>(section)->classDefinition;
}

std::optional<Json> encodePortableConstraintType(const DataType &type, const ClassIdentityPaths &classPaths) {
	if (type.kind == DataType::Kind::Function || type.typeExpression || type.functionSignature ||
		type.referencedKind == DataType::Kind::Function || type.classInstIndex != -1)
		return std::nullopt;
	Json classPath = nullptr;
	if (type.classDefinition) {
		auto path = classPaths.find(type.classDefinition);
		if (path == classPaths.end())
			return std::nullopt;
		classPath = path->second;
	}
	if ((type.kind == DataType::Kind::Class || type.referencedKind == DataType::Kind::Class) != !classPath.is_null())
		return std::nullopt;
	Json element = nullptr;
	if (type.arrayElementType) {
		auto encoded = encodePortableConstraintType(*type.arrayElementType, classPaths);
		if (!encoded)
			return std::nullopt;
		element = std::move(*encoded);
	}
	return Json::array(
		{static_cast<int>(type.kind), type.numericSize, type.pointerDepth, static_cast<int>(type.referencedKind),
		 type.arraySize, type.matrixRowCount, std::move(element), std::move(classPath)}
	);
}

DataType decodePortableConstraintType(const Json &encoded, const std::map<std::string, Section *> &sections) {
	DataType type;
	type.kind = static_cast<DataType::Kind>(encoded.at(0).get<int>());
	type.numericSize = encoded.at(1).get<int>();
	type.pointerDepth = encoded.at(2).get<int>();
	type.referencedKind = static_cast<DataType::Kind>(encoded.at(3).get<int>());
	type.arraySize = encoded.at(4).get<int>();
	type.matrixRowCount = encoded.at(5).get<int>();
	if (!encoded.at(6).is_null())
		type.arrayElementType = std::make_shared<DataType>(decodePortableConstraintType(encoded.at(6), sections));
	if (!encoded.at(7).is_null())
		type.classDefinition = constraintClassForPath(encoded.at(7), sections);
	return type;
}

std::optional<Json> encodePortableConstraint(const TypeConstraint &constraint, const ClassIdentityPaths &classPaths) {
	if (!constraint.isResolved() || constraint.classInstantiationIndex || constraint.functionSignature)
		return std::nullopt;
	Json classPath = nullptr;
	if (constraint.classDefinition) {
		auto path = classPaths.find(constraint.classDefinition);
		if (path == classPaths.end())
			return std::nullopt;
		classPath = path->second;
	}
	if (constraint.constrainsClassDefinition != !classPath.is_null())
		return std::nullopt;
	Json element = nullptr;
	if (constraint.elementConstraint) {
		auto encoded = encodePortableConstraint(*constraint.elementConstraint, classPaths);
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
		 constraint.requiresCompileTimeValue, std::move(element), std::move(classPath)}
	);
}

TypeConstraint decodePortableConstraint(const Json &encoded, const std::map<std::string, Section *> &sections) {
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
		constraint.elementConstraint = std::make_shared<TypeConstraint>(decodePortableConstraint(encoded.at(9), sections));
	if (!encoded.at(10).is_null()) {
		constraint.constrainsClassDefinition = true;
		constraint.classDefinition = constraintClassForPath(encoded.at(10), sections);
	}
	return constraint;
}

std::optional<Json>
encodePortableConstraintValue(const ParseContext::IncrementalConstraintValue &value, const ClassIdentityPaths &classPaths) {
	auto constraint = encodePortableConstraint(value.constraint, classPaths);
	auto type = encodePortableConstraintType(value.parameterType, classPaths);
	if (!constraint || !type)
		return std::nullopt;
	return Json::array({std::move(*constraint), std::move(*type)});
}

ParseContext::IncrementalConstraintValue
decodePortableConstraintValue(const Json &encoded, const std::map<std::string, Section *> &sections) {
	return {decodePortableConstraint(encoded.at(0), sections), decodePortableConstraintType(encoded.at(1), sections)};
}
