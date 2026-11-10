std::string
declarationDigest(const std::map<std::string, Section *> &sections, const std::unordered_set<const Section *> &excluded = {}) {
	Json declarations = Json::array();
	for (const auto &[path, section] : sections) {
		if (section->patternDefinitions.empty() || excluded.contains(section))
			continue;
		Json entry = sectionDeclaration(*section);
		entry["path"] = path;
		declarations.push_back(std::move(entry));
	}
	return digest(declarations.dump());
}
