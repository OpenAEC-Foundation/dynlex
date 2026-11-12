Json inferenceValueFingerprints(const Json &arguments, const Json &value) {
	return Json::array({digest(arguments.dump()), digest(value.dump())});
}

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

std::optional<Json> encodeInferenceValue(const CompileTimeValue &value, const InferenceValueIdentities &identities) {
	const ClassIdentityPaths &classes = identities.classes;
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
	if (const auto *function = std::get_if<CompileTimeFunction>(&value)) {
		if (identities.stagedFunction)
			if (auto encoded = identities.stagedFunction(*function))
				return encoded;
		const auto &definitions = function->definition->section->patternDefinitions;
		auto definition = std::ranges::find(definitions, function->definition);
		requireCompilerInvariant(definition != definitions.end(), "function value has an unowned definition");
		const Instantiation &instantiation = *function->instantiation;
		if (!instantiation.valid || instantiation.inferring ||
			(instantiation.needsReinfer && !instantiation.inferenceReusedFromCache) ||
			!instantiation.requiredCompileTimeParameters.empty())
			return std::nullopt;
		Json arguments = Json::array();
		for (const DataType &type : instantiation.argumentTypes) {
			std::set<std::pair<const ClassDefinition *, int>> visiting;
			auto encoded = inferenceTypeIdentity(type, classes, visiting);
			if (!encoded)
				return std::nullopt;
			arguments.push_back(std::move(*encoded));
		}
		std::set<std::pair<const ClassDefinition *, int>> visiting;
		auto result = inferenceTypeIdentity(instantiation.returnType, classes, visiting);
		if (!result)
			return std::nullopt;
		return Json::array(
			{"function", identities.paths.at(function->definition->section),
			 static_cast<size_t>(definition - definitions.begin()), function->pathIndex, std::move(arguments),
			 std::move(*result)}
		);
	}
	return std::nullopt;
}

Json captureInferenceValueQuery(
	const std::vector<CompileTimeValue> &arguments, const CompileTimeValue &value, const InferenceValueIdentities &identities
) {
	Json encodedArguments = Json::array();
	for (const CompileTimeValue &argument : arguments) {
		auto encoded = encodeInferenceValue(argument, identities);
		if (!encoded)
			return Json::array({nullptr, nullptr});
		encodedArguments.push_back(std::move(*encoded));
	}
	auto result = encodeInferenceValue(value, identities);
	return Json::array({digest(encodedArguments.dump()), result ? Json(digest(result->dump())) : Json(nullptr)});
}

std::optional<CompileTimeValue> decodeInferenceValue(const Json &encoded, const InferenceValueIdentities &identities) {
	const ClassIdentityPaths &classes = identities.classes;
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
	if (kind == "function") {
		requireCompilerInvariant(
			static_cast<bool>(identities.restoreFunction), "function value decoder has no restoration context"
		);
		auto function = identities.restoreFunction(encoded);
		return function ? std::optional<CompileTimeValue>{*function} : std::nullopt;
	}
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
