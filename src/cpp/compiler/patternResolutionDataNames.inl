// A named data declaration changes the lexical matcher environment. Previously
// matched calls can now be variables, including operands inside a larger match.
static std::vector<PatternReference *> referencesAffectedByDataNames(
	const std::vector<PatternReference *> &references, const std::unordered_set<Section *> &changedScopes
) {
	std::unordered_map<const Section *, std::vector<std::vector<std::string>>> names;
	for (const Section *section : changedScopes)
		for (const std::string &name : section->dataVariableDeclarations) {
			std::vector<std::string> required;
			for (const PatternElement &element : getPatternElements(name))
				if (element.type == PatternElement::Type::VariableLike)
					required.push_back(element.text);
			names[section].push_back(std::move(required));
		}
	std::vector<PatternReference *> affected;
	for (PatternReference *reference : references) {
		if (reference->resolved && !reference->match)
			continue;
		std::vector<PatternElement> elements = getPatternElements(reference->range().subString);
		std::unordered_set<std::string_view> words;
		for (const PatternElement &element : elements)
			if (element.type == PatternElement::Type::VariableLike)
				words.insert(element.text);
		Section *scope = reference->matchingScope ? reference->matchingScope : reference->range().section();
		for (const Section *section = scope; section; section = section->parent) {
			auto changed = names.find(section);
			if (changed == names.end())
				continue;
			if (std::ranges::any_of(changed->second, [&](const auto &required) {
				return std::ranges::all_of(required, [&](const std::string &word) {
					return words.contains(word);
				});
			})) {
				affected.push_back(reference);
				break;
			}
		}
	}
	return affected;
}
