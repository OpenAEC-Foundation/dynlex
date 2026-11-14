void collectSections(Section *section, const std::string &path, std::map<std::string, Section *> &sections) {
	visitSectionIdentities(section, path, [&](Section *child, const std::string &childPath) {
		sections.emplace(childPath, child);
	});
}
