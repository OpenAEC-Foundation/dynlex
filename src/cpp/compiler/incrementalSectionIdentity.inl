Json sectionIdentity(const Section &section) {
	const CodeLine *opening = section.openingLine;
	if (!opening && !section.patternDefinitions.empty())
		opening = section.patternDefinitions.front()->range.line;
	requireCompilerInvariant(opening && opening->sourceFile, "incremental section has no source identity");
	return Json{
		{"uri", opening->sourceFile->uri},
		{"header",
		 section.openingLine ? std::string(opening->rightTrimmedText) : section.patternDefinitions.front()->toString()},
		{"type", static_cast<int>(section.type)}
	};
}

void collectSections(Section *section, const std::string &path, std::map<std::string, Section *> &sections) {
	sections.emplace(path, section);
	std::map<std::string, size_t> duplicateIdentities;
	for (size_t index = 0; index < section->children.size(); index++) {
		Section *child = section->children[index];
		const CodeLine *source = child->openingLine;
		if (!source && !child->patternDefinitions.empty())
			source = child->patternDefinitions.front()->range.line;
		std::string component;
		if (source && source->sourceFile) {
			std::string identity = digest(sectionIdentity(*child).dump());
			component = "s" + identity + "-" + std::to_string(duplicateIdentities[identity]++);
		} else {
			component = "a" + std::to_string(index);
		}
		collectSections(child, path + "/" + component, sections);
	}
}
