std::string precedenceRulesSignature(const Section &root) {
	Json rules = Json::array();
	const auto visit = [&](auto &&self, const Section &section) -> void {
		if (!section.patternDefinitions.empty() && (!section.beforePatterns.empty() || !section.afterPatterns.empty() ||
													section.associativity != PatternAssociativity::Unspecified)) {
			Json rule = sectionDeclaration(section);
			const CodeLine *opening =
				section.openingLine ? section.openingLine : section.patternDefinitions.front()->range.line;
			requireCompilerInvariant(opening != nullptr, "precedence declaration has no source line");
			rule["source_line"] = opening->sourceFileLineIndex;
			rules.push_back(std::move(rule));
		}
		for (const Section *child : section.children)
			self(self, *child);
	};
	visit(visit, root);
	return digest(rules.dump());
}
