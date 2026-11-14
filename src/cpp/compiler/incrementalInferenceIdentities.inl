// Values refer to semantic definitions; compiler object addresses are local to
// one compilation and cannot identify a function in a persistent record.
struct InferenceValueIdentities {
	explicit InferenceValueIdentities(const std::map<const Section *, std::string> &paths)
		: paths(paths), classes(classIdentityPaths(paths)) {
		for (const auto &[section, path] : paths)
			sections.emplace(path, const_cast<Section *>(section));
	}
	const std::map<const Section *, std::string> &paths;
	ClassIdentityPaths classes;
	std::map<std::string, Section *> sections;
	std::function<std::optional<CompileTimeFunction>(const Json &)> restoreFunction;
	std::function<std::optional<Json>(const CompileTimeFunction &)> stagedFunction;
};
