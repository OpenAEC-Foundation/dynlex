static void addMatcherStartLiterals(std::string_view text, std::unordered_set<std::string_view> &literals) {
	for (size_t runStart = 0; runStart < text.size();) {
		auto category = [](unsigned char character) {
			if (std::isalnum(character) || character == '_')
				return 0;
			return character == ' ' ? 1 : 2;
		};
		int runCategory = category(static_cast<unsigned char>(text[runStart]));
		size_t runEnd = runStart + 1;
		while (runEnd < text.size() && category(static_cast<unsigned char>(text[runEnd])) == runCategory)
			runEnd++;
		if (runCategory == 0)
			literals.insert(text.substr(runStart, runEnd - runStart));
		else
			for (size_t start = runStart; start < runEnd; start++)
				for (size_t end = start + 1; end <= runEnd; end++)
					literals.insert(text.substr(start, end - start));
		runStart = runEnd;
	}
}

static std::unordered_set<std::string_view> referenceStartLiterals(const std::map<std::string, Section *> &sections) {
	std::unordered_set<std::string_view> literals;
	std::unordered_set<const CodeLine *> lines;
	for (const auto &[_, section] : sections)
		for (const PatternReference *reference : section->patternReferences) {
			if (lines.insert(reference->range().line).second)
				addMatcherStartLiterals(reference->range().line->fullText, literals);
			addMatcherStartLiterals(reference->pattern.text, literals);
			if (reference->expression)
				for (const Expression *argument : reference->expression->arguments)
					if (argument && argument->range.line && lines.insert(argument->range.line).second)
						addMatcherStartLiterals(argument->range.line->fullText, literals);
		}
	return literals;
}

static bool inactiveDefinitionCanChangeAnotherMatcherKey(
	const Section &candidate, const std::unordered_set<std::string_view> &referenceLiterals
) {
	for (const PatternDefinition *definition : candidate.patternDefinitions) {
		if (definition->patternElements.empty())
			return true;
		const DefinitionPatternElement &start = definition->patternElements.front();
		if (start.type == PatternElement::Type::Variable || start.text.empty() || !start.alternatives.empty() ||
			referenceLiterals.contains(start.text))
			return true;
	}
	return false;
}

std::vector<Section *> selectInactiveBodiesForResolutionReuse(
	const ParseContext &context, const Json &observations, const Json &prior, const Json &priorInactive,
	const std::set<std::string> &diagnosticSources, const IncrementalMatcherCache &matcherCache,
	std::unordered_map<const Section *, const Json *> &matchEntries
) {
	std::map<std::string, Section *> sections;
	collectSections(context.mainSection, "root", sections);
	Json selected = Json::object();
	std::unordered_set<const Section *> selectedSections;
	struct Candidate {
		std::string path;
		Section *section;
		const Json *entries;
	};
	std::vector<Candidate> candidates;
	size_t totalReferences = 0;
	size_t possibleSkippedReferences = 0;
	for (const auto &[path, section] : sections) {
		totalReferences += section->patternReferences.size();
		if (section->type == SectionType::Function && !section->patternDefinitions.empty() &&
			!containsNestedDefinition(*section)) {
			selected[path] = sectionIdentity(*section);
			selectedSections.insert(section);
		}
		if (section->parent != context.mainSection || !reusableCodegenSection(*section) ||
			!priorInactive.at("inactive").contains(path) || !prior.at("bodies").contains(path) ||
			!prior.at("function_declarations").contains(path) ||
			sectionDeclaration(*section) != prior.at("function_declarations").at(path) ||
			!prior.at("match_entries").contains(path) || diagnosticSources.contains(section->openingLine->sourceFile->uri))
			continue;
		const Json &entries = prior.at("match_entries").at(path);
		possibleSkippedReferences += entries.size();
		candidates.push_back({path, section, &entries});
	}
	if (candidates.empty() || possibleSkippedReferences * 20 < totalReferences || selected != prior.at("inactive"))
		return {};
	Json sources = sourceProjection(context, observations, selectedSections, false, false, false, &prior.at("sources"));
	if (!sourceProjectionsEqual(sources, prior.at("sources")))
		return {};
	Json bodies = functionBodyDigests(context, sections);
	std::unordered_set<std::string_view> referenceLiterals = referenceStartLiterals(sections);
	std::vector<Section *> result;
	for (const Candidate &candidate : candidates) {
		if (!bodies.contains(candidate.path) || bodies.at(candidate.path) != prior.at("bodies").at(candidate.path) ||
			inactiveDefinitionCanChangeAnotherMatcherKey(*candidate.section, referenceLiterals))
			continue;
		bool available = true;
		for (const Json &entry : *candidate.entries) {
			if (entry.at("key").is_string() && entry.at("initial_key").is_string() &&
				entry.at("key") != entry.at("initial_key"))
				available = false;
			for (const char *field : {"key", "initial_key"})
				if (entry.at(field).is_string() && !matcherCache.contains(entry.at(field).get_ref<const std::string &>()))
					available = false;
		}
		if (!available)
			continue;
		matchEntries.emplace(candidate.section, candidate.entries);
		result.push_back(candidate.section);
	}
	return result;
}
