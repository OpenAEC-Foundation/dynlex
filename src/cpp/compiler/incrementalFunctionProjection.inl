Json captureAllFunctionProjection(const ParseContext &context, const Json *previous = nullptr) {
	std::map<std::string, Section *> sections;
	collectSections(context.mainSection, "root", sections);
	Json selected = Json::object();
	Json declarations = Json::object();
	Json matches = Json::object();
	std::map<const Section *, std::string> bodies;
	std::map<const Section *, std::string> paths;
	std::unordered_set<const Section *> reusable;
	for (const auto &[path, section] : sections) {
		paths.emplace(section, path);
		if (section->type == SectionType::Function && !section->patternDefinitions.empty()) {
			if (!containsNestedDefinition(*section))
				selected[path] = sectionIdentity(*section);
			bodies.emplace(section, "");
		}
		if (reusableFunctionDeclaration(*section)) {
			reusable.insert(section);
			declarations[path] = sectionDeclaration(*section);
		}
	}
	for (const CodeLine *line : context.codeLines) {
		for (const Section *section = line->section; section; section = section->parent) {
			auto body = bodies.find(section);
			if (body != bodies.end()) {
				body->second.append(line->rightTrimmedText);
				body->second += '\n';
			}
		}
	}
	Json projection = captureInactiveProjection(context, &selected);
	Json bodyDigests = Json::object();
	for (const auto &[path, section] : sections) {
		if (auto body = bodies.find(section); body != bodies.end())
			bodyDigests[path] = digest(body->second);
	}
	projection["bodies"] = std::move(bodyDigests);
	projection["function_declarations"] = std::move(declarations);
	projection["ambient_declarations"] = declarationDigest(sections, reusable);
	projection["ambient_sources"] = sourceProjection(context, reusable, true, true);
	const Json *prior =
		previous && previous->contains("all_function_projection") ? &previous->at("all_function_projection") : nullptr;
	bool stableMatchingEnvironment = prior && projection.at("declarations") == prior->at("declarations") &&
									 projection.at("sources") == prior->at("sources") &&
									 projection.at("inactive") == prior->at("inactive");
	for (const Section *section : reusable) {
		const std::string &path = paths.at(section);
		if (stableMatchingEnvironment && prior->at("bodies").contains(path) &&
			prior->at("bodies").at(path) == projection.at("bodies").at(path) &&
			prior->at("function_declarations").at(path) == projection.at("function_declarations").at(path))
			matches[path] = prior->at("matches").at(path);
		else
			matches[path] = resolvedFunctionMatches(section, paths);
	}
	projection["matches"] = std::move(matches);
	return projection;
}
