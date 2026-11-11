std::optional<Json> inferenceConstraintIdentity(const TypeConstraint &constraint, const ClassIdentityPaths &classes) {
	auto properties = encodePortableConstraint(constraint, classes);
	if (!properties)
		return std::nullopt;
	Json layouts = Json::array();
	auto captureClass = [&](ClassDefinition *definition, int index) {
		if (index < 0)
			return true;
		std::set<std::pair<const ClassDefinition *, int>> visiting;
		auto layout = inferenceTypeIdentity(DataType{DataType::Kind::Class, 0, 0, definition, index}, classes, visiting);
		if (!layout)
			return false;
		layouts.push_back(std::move(*layout));
		return true;
	};
	std::function<bool(const DataType &)> captureType = [&](const DataType &type) {
		if (type.classDefinition && !captureClass(type.classDefinition, type.classInstIndex))
			return false;
		if (type.arrayElementType && !captureType(*type.arrayElementType))
			return false;
		if (type.functionSignature) {
			if (!captureType(type.functionSignature->result))
				return false;
			for (const DataType &parameter : type.functionSignature->parameters)
				if (!captureType(parameter))
					return false;
		}
		return true;
	};
	std::function<bool(const TypeConstraint &)> capture = [&](const TypeConstraint &value) {
		if (value.classInstantiationIndex && !captureClass(value.classDefinition, *value.classInstantiationIndex))
			return false;
		if (value.elementConstraint && !capture(*value.elementConstraint))
			return false;
		if (value.functionSignature) {
			if (!captureType(value.functionSignature->result))
				return false;
			for (const DataType &parameter : value.functionSignature->parameters)
				if (!captureType(parameter))
					return false;
		}
		return true;
	};
	if (!capture(constraint))
		return std::nullopt;
	return Json::array({std::move(*properties), std::move(layouts)});
}

std::optional<Json> encodeInferenceValue(const CompileTimeValue &value, const ClassIdentityPaths &classes) {
	if (const auto *integer = std::get_if<WideInteger>(&value))
		return Json::array({"wide", integer->bits.getBitWidth(), integer->isUnsigned, integer->toString(16)});
	if (const auto *integer = std::get_if<std::int64_t>(&value))
		return Json::array({"signed", *integer});
	if (const auto *integer = std::get_if<std::uint64_t>(&value))
		return Json::array({"unsigned", *integer});
	if (const auto *number = std::get_if<double>(&value))
		return Json::array({"float_bits", std::bit_cast<std::uint64_t>(*number)});
	if (const auto *text = std::get_if<std::string>(&value))
		return Json::array({"text", *text});
	if (const auto *boolean = std::get_if<bool>(&value))
		return Json::array({"boolean", *boolean});
	if (const auto *pointer = std::get_if<CompileTimePointer>(&value))
		return Json::array({"pointer", pointer->address});
	if (const auto *reference = std::get_if<TypeReferenceValue>(&value)) {
		std::set<std::pair<const ClassDefinition *, int>> visiting;
		auto type = inferenceTypeIdentity(reference->type, classes, visiting);
		auto constraint = inferenceConstraintIdentity(reference->constraint, classes);
		if (!type || !constraint)
			return std::nullopt;
		return Json::array({"type", std::move(*type), std::move(*constraint)});
	}
	if (const auto *constraint = std::get_if<TypeConstraint>(&value)) {
		auto properties = inferenceConstraintIdentity(*constraint, classes);
		if (!properties)
			return std::nullopt;
		return Json::array({"constraint", std::move(*properties)});
	}
	return std::nullopt;
}

std::optional<CompileTimeValue> decodeInferenceValue(const Json &encoded, const ClassIdentityPaths &classes) {
	const std::string &kind = encoded.at(0).get_ref<const std::string &>();
	if (kind == "wide")
		return WideInteger{
			llvm::APInt(encoded.at(1).get<unsigned>(), encoded.at(3).get<std::string>(), 16), encoded.at(2).get<bool>()
		};
	if (kind == "signed")
		return encoded.at(1).get<std::int64_t>();
	if (kind == "unsigned")
		return encoded.at(1).get<std::uint64_t>();
	if (kind == "float_bits")
		return std::bit_cast<double>(encoded.at(1).get<std::uint64_t>());
	if (kind == "text")
		return encoded.at(1).get<std::string>();
	if (kind == "boolean")
		return encoded.at(1).get<bool>();
	if (kind == "pointer")
		return CompileTimePointer{encoded.at(1).get<std::uint64_t>()};
	if (kind == "type" || kind == "constraint") {
		const Json &properties = encoded.at(kind == "type" ? 2 : 1);
		if (!portableConstraintReferencesPresent(properties.at(0), classes))
			return std::nullopt;
		TypeConstraint constraint = decodePortableConstraint(properties.at(0), classes);
		auto identity = inferenceConstraintIdentity(constraint, classes);
		if (!identity || *identity != properties)
			return std::nullopt;
		if (kind == "constraint")
			return constraint;
		auto type = decodeInferenceTypeIdentity(encoded.at(1), classes);
		if (!type)
			return std::nullopt;
		requireCompilerInvariant(type->kind == DataType::Kind::Type, "cached type value has a non-type representation");
		return TypeReferenceValue{std::move(*type), std::move(constraint)};
	}
	crashCompilerBug("cached inference contains an unknown compile-time value kind");
}
