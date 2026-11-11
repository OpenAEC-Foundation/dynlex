static void initializeSectionSymbolIdentities(ParseContext &context) {
	if (!context.functionSymbolIdentities.empty())
		return;
	visitSectionIdentities(context.mainSection, "root", [&](const Section *section, const std::string &path) {
		if (hasCodegenSectionIdentity(*section))
			context.functionSymbolIdentities.emplace(section, sectionIdentityDigest(path));
	});
}

using ClassLayoutStack = std::vector<std::pair<const ClassDefinition *, int>>;

static void
appendCodegenTypeIdentity(ParseContext &context, const DataType &type, ClassLayoutStack &layouts, std::string &identity) {
	identity += std::to_string(static_cast<int>(type.kind)) + "," + std::to_string(type.numericSize) + "," +
				std::to_string(type.pointerDepth) + "," + std::to_string(static_cast<int>(type.referencedKind)) + "," +
				std::to_string(type.arraySize) + "," + std::to_string(type.matrixRowCount) + ";";
	if (type.arrayElementType)
		appendCodegenTypeIdentity(context, *type.arrayElementType, layouts, identity);
	if (type.functionSignature) {
		identity += "(";
		for (const DataType &parameter : type.functionSignature->parameters)
			appendCodegenTypeIdentity(context, parameter, layouts, identity);
		identity += ")->";
		appendCodegenTypeIdentity(context, type.functionSignature->result, layouts, identity);
	}
	if (!type.classDefinition)
		return;

	const ClassDefinition &definition = *type.classDefinition;
	const CodeLine *opening = definition.range.line;
	requireCompilerInvariant(opening && opening->sectionOpening, "codegen class type has no definition section");
	identity += context.functionSymbolIdentities.at(opening->sectionOpening);
	if (type.classInstIndex < 0) {
		requireCompilerInvariant(type.classInstIndex == -1, "symbolic class layout reached codegen");
		identity += "?";
		return;
	}

	auto layout = std::pair{&definition, type.classInstIndex};
	if (auto active = std::ranges::find(layouts, layout); active != layouts.end()) {
		identity += "@" + std::to_string(active - layouts.begin()) + ";";
		return;
	}
	layouts.push_back(layout);
	identity += "{";
	for (const DataType &field : definition.instantiations.at(type.classInstIndex).fieldTypes)
		appendCodegenTypeIdentity(context, field, layouts, identity);
	identity += "}";
	layouts.pop_back();
}

static void appendInternalFunctionIdentity(
	ParseContext &context, const Section *section, const std::vector<DataType> &argumentTypes, std::string &name
) {
	std::string identity = context.functionSymbolIdentities.at(section);
	ClassLayoutStack layouts;
	for (const DataType &type : argumentTypes) {
		identity += "(";
		appendCodegenTypeIdentity(context, type, layouts, identity);
		identity += ")";
	}
	name += "." + sectionIdentityDigest(identity);
}

static void appendCodegenConstraintIdentity(
	ParseContext &context, const TypeConstraint &constraint, ClassLayoutStack &layouts, std::string &identity
) {
	auto append = [&](const auto &value) {
		identity += value ? std::to_string(static_cast<long long>(*value)) : "?";
		identity += ";";
	};
	identity += constraint.isResolved() ? "resolved;" : "pending;";
	append(constraint.kind);
	append(constraint.numericSize);
	append(constraint.pointerDepth);
	append(constraint.arraySize);
	append(constraint.matrixRows);
	append(constraint.matrixColumns);
	append(constraint.classInstantiationIndex);
	identity += std::to_string(constraint.requiresNumeric) + std::to_string(constraint.requiresInteger) +
				std::to_string(constraint.requiresCompileTimeValue) + std::to_string(constraint.constrainsClassDefinition);
	identity += ";class[";
	if (constraint.classDefinition)
		appendCodegenTypeIdentity(
			context,
			DataType{DataType::Kind::Class, 0, 0, constraint.classDefinition, constraint.classInstantiationIndex.value_or(-1)},
			layouts, identity
		);
	identity += "];element[";
	if (constraint.elementConstraint)
		appendCodegenConstraintIdentity(context, *constraint.elementConstraint, layouts, identity);
	identity += "];function[";
	if (constraint.functionSignature) {
		appendCodegenTypeIdentity(context, constraint.functionSignature->result, layouts, identity);
		for (const DataType &parameter : constraint.functionSignature->parameters)
			appendCodegenTypeIdentity(context, parameter, layouts, identity);
	}
	identity += "];";
}

static std::string codegenConstraintIdentity(ParseContext &context, const TypeConstraint &constraint) {
	ClassLayoutStack layouts;
	std::string identity;
	appendCodegenConstraintIdentity(context, constraint, layouts, identity);
	return sectionIdentityDigest(identity);
}

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

static std::string encodeInstantiationKeyForFunctionName(ParseContext &context, const InstantiationKey &instantiationKey) {
	std::string suffix;
	for (const auto &[name, value] : instantiationKey.compileTimeParameters) {
		suffix += "_ct_" + encodeLLVMNameComponent(name) + "_";
		if (const auto *integer = std::get_if<WideInteger>(&value))
			suffix += std::string(integer->isUnsigned ? "wu" : "wi") + std::to_string(integer->bits.getBitWidth()) + "_" +
					  encodeLLVMNameComponent(integer->toString(16));
		else if (const auto *integer = std::get_if<std::int64_t>(&value))
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
		else if (const auto *typeRef = std::get_if<TypeReferenceValue>(&value)) {
			suffix += encodeLLVMNameComponent(typeRef->type.toReferencedType().toString());
			suffix += "_constraint_" + codegenConstraintIdentity(context, typeRef->constraint);
		} else if (const auto *constraint = std::get_if<TypeConstraint>(&value))
			suffix += "constraint_" + codegenConstraintIdentity(context, *constraint);
		else if (const auto *pointer = std::get_if<CompileTimePointer>(&value))
			suffix += "ptr" + std::to_string(pointer->address);
		else if (const auto *function = std::get_if<CompileTimeFunction>(&value)) {
			const Range &range = function->definition->range;
			suffix += "fn" + std::to_string(range.line->mergedLineIndex) + "_" + std::to_string(range.start()) + "_" +
					  std::to_string(function->pathIndex);
			for (const DataType &type : function->instantiation->argumentTypes)
				suffix += "_" + encodeLLVMNameComponent(type.toString());
		} else
			suffix += "unknown";
	}
	return suffix;
}
