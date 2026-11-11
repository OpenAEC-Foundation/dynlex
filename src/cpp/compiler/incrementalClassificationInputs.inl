// Classification reads pattern references and lexical bindings, not literal-only
// execution statements. Keep the latter in the separate body-resolution digest.
std::string IncrementalClassification::classificationBodyInput(const Unit &unit) const {
	if (containsNestedDefinition(*unit.section))
		return counts.bodies().at(unit.path).get<std::string>();
	std::vector<std::pair<int, int>> anchors;
	for (const PatternReference *reference : unit.references)
		anchors.emplace_back(reference->range().line->mergedLineIndex, reference->range().start());
	std::ranges::sort(anchors);
	auto location = [&](const Range &range) {
		auto position = std::pair(range.line->mergedLineIndex, range.start());
		return Json::array({range.line->sourceFile->uri, std::ranges::lower_bound(anchors, position) - anchors.begin()});
	};
	auto scopeInput = [&](auto &&self, const Section &scope) -> Json {
		Json references = Json::array();
		for (const PatternReference *reference : scope.patternReferences) {
			const Range &range = reference->range();
			Json matchingScope = nullptr;
			if (reference->matchingScope)
				matchingScope =
					reference->matchingScope == context.mainSection ? Json("root") : sectionIdentity(*reference->matchingScope);
			references.push_back(
				Json::array(
					{range.line->sourceFile->uri, reference->pattern.text, std::string(range.subString),
					 static_cast<int>(reference->patternType), static_cast<int>(reference->purpose), std::move(matchingScope)}
				)
			);
		}
		Json explicitDeclarations = Json::object(), definitions = Json::object();
		for (const auto &[name, range] : scope.explicitVariableDeclarations)
			explicitDeclarations[name] = location(range);
		for (const auto &[name, definition] : scope.variableDefinitions)
			definitions[name] = location(definition->range);
		std::vector<std::string> dataNames(scope.dataVariableDeclarations.begin(), scope.dataVariableDeclarations.end());
		std::ranges::sort(dataNames);
		Json children = Json::array();
		for (const Section *child : scope.children)
			children.push_back(self(self, *child));
		return Json::array(
			{sectionIdentity(scope), std::move(references), std::move(explicitDeclarations), std::move(definitions),
			 std::move(dataNames), std::move(children)}
		);
	};
	return digest(scopeInput(scopeInput, *unit.section).dump());
}
