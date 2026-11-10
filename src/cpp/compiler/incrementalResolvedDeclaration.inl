Json resolvedDefinitionElements(const std::vector<DefinitionPatternElement> &elements) {
	Json result = Json::array();
	for (const DefinitionPatternElement &element : elements) {
		Json alternatives = Json::array();
		for (const auto &alternative : element.alternatives)
			alternatives.push_back(resolvedDefinitionElements(alternative));
		result.push_back(
			Json::array(
				{static_cast<int>(element.type), element.text, element.isArgumentPack, element.typeConstraintName,
				 element.promotedFromVariableLike, element.isExplicitLiteral, element.isLiteralInSingleWordSpelling,
				 std::move(alternatives)}
			)
		);
	}
	return result;
}

Json sectionDeclaration(const Section &section) {
	Json definitions = Json::array();
	for (const PatternDefinition *definition : section.patternDefinitions)
		definitions.push_back(
			Json{{"spelling", definition->toString()}, {"elements", resolvedDefinitionElements(definition->patternElements)}}
		);
	return Json{
		{"identity", sectionIdentity(section)},
		{"definitions", definitions},
		{"local", section.isLocal},
		{"flex", section.isFlex},
		{"exposed", section.isExposed},
		{"conversion", section.isConversion},
		{"implicit_conversion", section.isImplicitConversion},
		{"return_contract", static_cast<int>(section.returnContract)},
		{"before", section.beforePatterns},
		{"after", section.afterPatterns},
		{"associativity", static_cast<int>(section.associativity)}
	};
}
