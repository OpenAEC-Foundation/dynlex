static std::string encodeLLVMNameComponent(std::string_view value) {
	static constexpr char hexadecimalDigits[] = "0123456789abcdef";
	std::string encoded;
	for (unsigned char character : value) {
		if (std::isalnum(character)) {
			encoded += static_cast<char>(character);
			continue;
		}
		encoded += '_';
		encoded += hexadecimalDigits[character >> 4];
		encoded += hexadecimalDigits[character & 0x0f];
		encoded += '_';
	}
	return encoded;
}

static std::string encodeLLVMTypeName(ParseContext &context, const DataType &type) {
	auto known = context.codegenTypeIdentities.find(type);
	if (known != context.codegenTypeIdentities.end())
		return known->second;
	std::string name = encodeLLVMNameComponent(type.toString());
	if (!type.isMetaType()) {
		auto identity = typeIdentity(type, context.codegenClassIdentities, TypeIdentityMode::Descriptor);
		requireCompilerInvariant(identity.has_value(), "function symbol has no type identity");
		name += "_type_" + stableIdentityDigest(identity->dump());
	}
	context.codegenTypeIdentities.emplace(type, name);
	return name;
}

static std::string encodeInstantiationKeyForFunctionName(ParseContext &context, const InstantiationKey &instantiationKey) {
	std::string suffix;
	for (const auto &[name, value] : instantiationKey.compileTimeParameters) {
		suffix += "_ct_" + encodeLLVMNameComponent(name) + "_";
		if (const auto *integer = std::get_if<std::int64_t>(&value))
			suffix += "i" + encodeLLVMNameComponent(std::to_string(*integer));
		else if (const auto *integer = std::get_if<std::uint64_t>(&value))
			suffix += "u" + encodeLLVMNameComponent(std::to_string(*integer));
		else if (std::holds_alternative<MinimumSignedIntegerMagnitude>(value))
			suffix += "i_minimum_magnitude";
		else if (const auto *number = std::get_if<double>(&value))
			suffix += std::to_string(std::bit_cast<uint64_t>(*number));
		else if (const auto *text = std::get_if<std::string>(&value))
			suffix += std::to_string(text->size()) + "_" + encodeLLVMNameComponent(*text);
		else if (const auto *boolean = std::get_if<bool>(&value))
			suffix += *boolean ? "true" : "false";
		else if (const auto *typeRef = std::get_if<TypeReferenceValue>(&value))
			suffix += encodeLLVMTypeName(context, typeRef->type.toReferencedType());
		else if (const auto *constraint = std::get_if<TypeConstraint>(&value))
			suffix += encodeLLVMNameComponent(constraint->toInternalString());
		else if (const auto *pointer = std::get_if<CompileTimePointer>(&value))
			suffix += "ptr" + std::to_string(pointer->address);
		else if (const auto *resource = std::get_if<ShaderResourceValue>(&value))
			suffix += "resource" + encodeLLVMNameComponent(resource->identity());
		else if (const auto *function = std::get_if<CompileTimeFunction>(&value)) {
			suffix += "fn" + getPatternFunctionName(context, function->definition->section) + "_" +
					  std::to_string(function->pathIndex);
			for (const DataType &type : function->instantiation->argumentTypes)
				suffix += "_" + encodeLLVMTypeName(context, type);
		} else
			suffix += "unknown";
	}
	return suffix;
}
