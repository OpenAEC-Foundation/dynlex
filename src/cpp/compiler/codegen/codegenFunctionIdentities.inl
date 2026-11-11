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
