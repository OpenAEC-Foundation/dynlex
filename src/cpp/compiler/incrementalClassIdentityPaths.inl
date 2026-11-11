using ClassIdentityPaths = std::map<const ClassDefinition *, std::string>;

ClassIdentityPaths classIdentityPaths(const std::map<const Section *, std::string> &sectionPaths) {
	ClassIdentityPaths result;
	for (const auto &[section, path] : sectionPaths)
		if (section->type == SectionType::Class)
			result.emplace(static_cast<const ClassSection *>(section)->classDefinition, path);
	return result;
}
