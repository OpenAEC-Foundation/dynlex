static std::vector<CallableFunctionMatch> findDefinitionPathsBySignature(
	ParseContext &context, SectionType sectionType, std::string_view signature, const lsp::SourceFile *sourceFile
) {
	std::string converted(signature);
	for (char &character : converted) {
		if (character == '$')
			character = argumentChar;
	}

	auto elements = getPatternElements(converted);
	PatternTreeNode *node = context.patternTrees[(int)sectionType];
	std::vector<PatternTreeNode *> nodesPassed;
	nodesPassed.reserve(elements.size());
	for (const PatternElement &element : elements) {
		if (!node)
			return {};
		if (element.type == PatternElement::Type::Variable) {
			node = node->argumentChild;
		} else {
			auto child = node->literalChildren.find(element.text);
			node = child != node->literalChildren.end() ? child->second : nullptr;
		}
		if (node)
			nodesPassed.push_back(node);
	}

	std::vector<CallableFunctionMatch> matches;
	if (!node)
		return matches;
	requireCompilerInvariant(sourceFile != nullptr, "signature lookup requires a source file");
	for (PatternDefinition *definition : node->matchingDefinitions) {
		requireCompilerInvariant(definition != nullptr, "pattern tree endpoint contains a null definition");
		if (!isPatternDefinitionVisibleFromSource(*definition, *sourceFile))
			continue;
		for (size_t pathIndex : matchingPatternPathIndices(nodesPassed, definition))
			matches.push_back({definition, pathIndex});
	}
	return matches;
}

std::vector<PatternDefinition *> findDefinitionsBySignature(
	ParseContext &context, SectionType sectionType, std::string_view signature, const lsp::SourceFile *sourceFile
) {
	std::vector<PatternDefinition *> definitions;
	for (const CallableFunctionMatch &match :
		 findDefinitionPathsBySignature(context, sectionType, signature, sourceFile)) {
		if (std::find(definitions.begin(), definitions.end(), match.definition) == definitions.end())
			definitions.push_back(match.definition);
	}
	return definitions;
}

std::vector<CallableFunctionMatch> findCallableFunctionsBySignature(
	ParseContext &context, std::string_view signature, const lsp::SourceFile *sourceFile
) {
	std::vector<CallableFunctionMatch> matches;
	for (const CallableFunctionMatch &match :
		 findDefinitionPathsBySignature(context, SectionType::Function, signature, sourceFile)) {
		PatternDefinition *definition = match.definition;
		if (definition && definition->section && definition->section->type == SectionType::Function &&
			!definition->section->isFlex)
			matches.push_back(match);
	}
	return matches;
}

static DataType concretizeCallableParameterType(DataType type) {
	if (type.kind == DataType::Kind::Class && type.classDefinition && type.classInstIndex == -1 &&
		type.classDefinition->instantiations.size() == 1)
		type.classInstIndex = 0;
	return type;
}

void collectCallableFunctionParameters(
	const CallableFunctionMatch &match, std::vector<CallableFunctionParameter> &outParameters
) {
	outParameters.clear();
	requireCompilerInvariant(match.definition != nullptr, "callable parameter collection requires a definition");
	requireCompilerInvariant(
		match.pathIndex < match.definition->indexedPaths.size() &&
			match.pathIndex < match.definition->signaturePaths.size(),
		"callable parameter collection requires an exact compiled signature path"
	);
	const auto &signatures = match.definition->signaturePaths[match.pathIndex].parameters;
	size_t parameterIndex = 0;
	forEachPatternParameterName(
		match.definition, match.pathIndex,
		[&](const std::string &name, PatternTreeNode *, size_t) {
			requireCompilerInvariant(
				parameterIndex < signatures.size(), "callable path has more parameters than its compiled signature"
			);
			const PatternParameterSignature &signature = signatures[parameterIndex++];
			outParameters.push_back(
				{name, concretizeCallableParameterType(signature.staticParameterType),
				 signature.requiresCompileTimeValue}
			);
		}
	);
	requireCompilerInvariant(
		parameterIndex == signatures.size(), "callable path has fewer parameters than its compiled signature"
	);
}
