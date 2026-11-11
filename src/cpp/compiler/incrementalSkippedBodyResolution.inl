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

#include "incrementalMatcherLiteralIndex.inl"

static bool cachedDefinitionCanChangeAnotherMatcherKey(
	const Section &candidate, const Json &priorLiterals, const std::unordered_set<std::string_view> &changedLiterals
) {
	for (const PatternDefinition *definition : candidate.patternDefinitions) {
		if (definition->patternElements.empty())
			return true;
		auto hasPromotable = [&](auto &&self, const std::vector<DefinitionPatternElement> &elements) -> bool {
			for (const DefinitionPatternElement &element : elements) {
				if (element.type == PatternElement::Type::Choice) {
					for (const auto &alternative : element.alternatives)
						if (self(self, alternative))
							return true;
				} else if (candidate.canPromoteImplicitParameter(*definition, element) &&
						   candidate.variableLikeCounts.at(element.text) != 0)
					return true;
			}
			return false;
		};
		if (!hasPromotable(hasPromotable, definition->patternElements))
			continue;
		const DefinitionPatternElement &start = definition->patternElements.front();
		if (start.type == PatternElement::Type::Variable || start.text.empty() || !start.alternatives.empty() ||
			priorLiterals.contains(llvm::toHex(llvm::StringRef(start.text), true)) || changedLiterals.contains(start.text))
			return true;
	}
	return false;
}

std::vector<Section *> selectCachedBodiesForResolutionReuse(
	const ParseContext &context, const Json &observations, const Json &prior, const std::set<std::string> &diagnosticSources,
	const IncrementalMatcherCache &matcherCache, const std::map<std::string, Section *> &sections, const Json &bodies,
	std::unordered_map<const Section *, const Json *> &matchEntries
) {
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
		if (section->parent != context.mainSection || !reusableCodegenSection(*section) || !prior.at("bodies").contains(path) ||
			!bodies.contains(path) || bodies.at(path) != prior.at("bodies").at(path) ||
			!prior.at("function_declarations").contains(path) ||
			!prior.at("match_entries").contains(path) || !prior.at("matcher_projection").at("body_units").contains(path) ||
			diagnosticSources.contains(section->openingLine->sourceFile->uri))
			continue;
		const Json &overrides = prior.at("matcher_projection").at("match_overrides");
		const Json &entries = overrides.contains(path) ? overrides.at(path) : prior.at("match_entries").at(path);
		possibleSkippedReferences += entries.size();
		candidates.push_back({path, section, &entries});
	}
	if (candidates.empty() || possibleSkippedReferences * 20 < totalReferences || selected != prior.at("inactive"))
		return {};
	Json sources = sourceProjection(context, observations, selectedSections, false, false, false, &prior.at("sources"));
	if (!sourceProjectionsEqual(sources, prior.at("sources")))
		return {};
	std::set<std::string> changedSources = changedReferenceLiteralSources(context, observations, prior);
	ReferenceLiteralsBySource changedBySource = referenceStartLiteralsBySource(sections, changedSources);
	std::unordered_set<std::string_view> changedLiterals;
	for (const auto &[_, literals] : changedBySource)
		changedLiterals.insert(literals.begin(), literals.end());
	std::erase_if(candidates, [&](const Candidate &candidate) {
		// A body with no occurrences of an implicit candidate cannot promote its
		// header. Its stable path and body preserve authored declaration metadata.
		bool canPromote = std::ranges::any_of(candidate.section->variableLikeCounts, [](const auto &candidate) {
			return candidate.second != 0;
		});
		if (canPromote && sectionDeclaration(*candidate.section) != prior.at("function_declarations").at(candidate.path))
			return true;
		return cachedDefinitionCanChangeAnotherMatcherKey(
			*candidate.section, prior.at("reference_literals_global"), changedLiterals
		);
	});
	size_t safeReferences = 0;
	for (const Candidate &candidate : candidates)
		safeReferences += candidate.entries->size();
	if (safeReferences * 20 < totalReferences)
		return {};
	std::vector<Section *> result;
	for (const Candidate &candidate : candidates) {
		bool available = true;
		for (const Json &entry : *candidate.entries) {
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
