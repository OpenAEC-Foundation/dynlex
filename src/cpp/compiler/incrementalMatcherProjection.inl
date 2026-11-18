struct MatcherProjectionWork {
	double outputs = 0, tree = 0, initial = 0, selection = 0, sources = 0, declarations = 0;
	double initialBodies = 0, final = 0, headers = 0, referenceTables = 0;
	double bindings = 0, classifications = 0, inactiveCopy = 0, publishCopy = 0;
	double ownerContexts = 0, sourceOwnerSelection = 0;
	double constraintSources = 0;
	size_t constraintSourceUnitsReused = 0, constraintSourceUnitsScanned = 0, constraintSourceReferencesScanned = 0;
};

// Preserve the matcher boundary before receiver bodies become generated functions.
// Later projections describe expanded sections and cannot identify this stage.
struct InitialMatcherProjection {
	MatcherProjectionWork *work;
	explicit InitialMatcherProjection(MatcherProjectionWork *work = nullptr) : work(work) {}
	IncrementalWorkTimer measure(double MatcherProjectionWork::*counter) const {
		return IncrementalWorkTimer(work ? &(work->*counter) : nullptr);
	}
	struct Owner {
		std::string path;
		const Section *section;
		bool topLevel, demandEligible;
		std::vector<PatternReference *> references;
		size_t referenceCount = 0;
		std::vector<CodeLine *> lines;
		std::vector<std::pair<size_t, PatternReference *>> deferredReferences;
	};
	Json value = nullptr;
	std::vector<Owner> owners;
	std::unordered_set<const Section *> constraintSourceOwners;
	bool unitsCaptured = false;
	const Json *priorUnits = nullptr, *published = nullptr;
	const Json &projection() const { return published ? *published : value; }
	size_t declarationsReused = 0;

	void capture(
		const ParseContext &context, const Json &observations, const std::list<PatternReference *> &deferredReferences,
		const Json *previous, const Json &bodyDigests, const std::map<std::string, Section *> &sections, std::string_view tree
	) {
		auto initialTimer = measure(&MatcherProjectionWork::initial);
		auto selectionTimer = measure(&MatcherProjectionWork::selection);
		const Json *prior = previous ? &previous->at("matcher_projection") : nullptr;
		priorUnits = prior;
		declarationsReused = 0;
		owners.clear();
		constraintSourceOwners.clear();
		unitsCaptured = false;
		std::unordered_set<const PatternReference *> deferred(deferredReferences.begin(), deferredReferences.end());
		std::unordered_set<const Section *> functionBodies, reusable;
		Json selected = Json::object();
		for (const auto &[path, section] : sections) {
			if (section->type == SectionType::Function && !section->patternDefinitions.empty() &&
				!containsNestedDefinition(*section)) {
				selected[path] = sectionIdentity(*section);
				functionBodies.insert(section);
			}
			if (!reusableResolutionSection(*section))
				continue;
			reusable.insert(section);
			bool topLevel = section->parent == context.mainSection;
			Owner owner{
				path,	  section,
				topLevel, topLevel && section->type == SectionType::Function && !section->isFlex && hasNoGlobalAccess(section),
				{},		  0,
				{},		  {}
			};
			owner.references = sourceOwnerReferences(*section);
			owner.lines = sourceOwnerLines(*section);
			owner.referenceCount = context.retainedSourceOwners.contains(section)
									   ? context.retainedSourceOwners.at(section).referenceCount
									   : owner.references.size();
			if (owner.referenceCount == 0)
				continue;
			for (size_t index = 0; index < owner.references.size(); index++)
				if (deferred.contains(owner.references[index]))
					owner.deferredReferences.emplace_back(index, owner.references[index]);
			owners.push_back(std::move(owner));
		}
		selectionTimer.stop();
		auto sourceView = [&](const char *field, const auto &masked, bool openings, bool runs, bool execution) {
			auto timer = measure(&MatcherProjectionWork::sources);
			return sourceProjection(
				context, observations, masked, openings, runs, execution, prior ? &prior->at(field) : nullptr
			);
		};
		value = Json{
			{"selected_digest", digest(selected.dump())},
			{"declaration_digests", Json::object()},
			{"match_overrides", Json::object()},
			{"source_match_units", Json::object()},
			{"source_match_results", Json::object()},
			{"constraint_sources", Json::object()},
			{"body_units", Json::object()},
			{"final_body_units", Json::object()},
			{"header_definitions", Json::object()},
			{"initial_header_definitions", Json::object()},
			{"bodies", bodyDigests},
			{"numeric_spellings", Json::object()},
			{"reference_sources", Json::object()},
			{"scope_rule_bodies", scopeRuleBodyPaths(sections)},
			{"global_resolution", globalResolutionIdentity(*context.mainSection)},
			{"sources", sourceView("sources", functionBodies, false, false, false)},
			{"ambient_sources", sourceView("ambient_sources", reusable, true, true, false)},
			{"ambient_nonexecution_sources", sourceView("ambient_nonexecution_sources", reusable, true, true, true)}
		};
		auto declarationTimer = measure(&MatcherProjectionWork::declarations);
		bool stableDeclarations =
			prior && previous->at("initial_matcher_tree") == tree &&
			value.at("scope_rule_bodies") == prior->at("scope_rule_bodies") &&
			sourceProjectionsEqual(value.at("ambient_nonexecution_sources"), prior->at("ambient_nonexecution_sources"));
		if (stableDeclarations)
			for (const auto &[path, _] : value.at("scope_rule_bodies").items())
				if (!prior->at("bodies").contains(path) || bodyDigests.at(path) != prior->at("bodies").at(path)) {
					stableDeclarations = false;
					break;
				}
		for (const Owner &owner : owners) {
			const std::string &path = owner.path;
			auto contextTimer = measure(&MatcherProjectionWork::ownerContexts);
			if (context.retainedSourceOwners.contains(owner.section)) {
				for (const char *field : {"numeric_spellings", "reference_sources"})
					value.at(field)[path] = prior->at(field).at(path);
			} else {
				Json numbers = Json::object(), sources = Json::array();
				const lsp::SourceFile *last = nullptr;
				for (size_t index = 0; index < owner.references.size(); index++) {
					PatternReference *reference = owner.references[index];
					const lsp::SourceFile *source = reference->range().line->sourceFile;
					if (source != last) {
						sources.push_back(Json::array({index, source->uri}));
						last = source;
					}
					std::set<std::string> spellings;
					if (reference->expression)
						for (size_t argument = 0; argument < reference->expression.argumentCount(); argument++)
							if (auto spelling = numericPatternArgumentSpelling(reference, argument))
								spellings.insert(*spelling);
					if (!spellings.empty())
						numbers[std::to_string(index)] = spellings;
				}
				value.at("numeric_spellings")[path] = std::move(numbers);
				value.at("reference_sources")[path] = std::move(sources);
			}
			contextTimer.stop();
			if (stableDeclarations && prior->at("declaration_digests").contains(path) && prior->at("bodies").contains(path) &&
				bodyDigests.at(path) == prior->at("bodies").at(path)) {
				value.at("declaration_digests")[path] = prior->at("declaration_digests").at(path);
				declarationsReused++;
			} else
				value.at("declaration_digests")[path] = digest(sectionDeclaration(*owner.section).dump());
		}
	}

	Json headerDefinitions(const Owner &owner, const std::map<const Section *, std::string> &paths) const {
		auto timer = measure(&MatcherProjectionWork::headers);
		Json headers = Json::array();
		for (size_t index = 0; index < owner.references.size(); index++) {
			PatternReference *reference = owner.references[index];
			if (!reference->range().line->sectionOpening || !reference->match ||
				reference->range().line->expression.rootReference() != reference)
				continue;
			Json definitions = Json::array();
			for (PatternDefinition *definition : reference->match->matchingDefinitions) {
				size_t occurrence = 0;
				for (const PatternDefinition *other : definition->section->patternDefinitions) {
					if (other == definition)
						break;
					occurrence += other->toString() == definition->toString();
				}
				definitions.push_back(Json::array({paths.at(definition->section), definition->toString(), occurrence}));
			}
			const auto &lines = owner.lines;
			auto line = std::ranges::find(lines, reference->range().line);
			requireCompilerInvariant(line != lines.end(), "header definition has no source-owner anchor");
			headers.push_back(Json::array({line - lines.begin(), std::move(definitions)}));
		}
		return headers;
	}

	void captureSourceMatchResults(
		const std::string &path, const std::vector<PatternReference *> &references, const Json &expanded,
		const std::map<const Section *, std::string> &paths, IncrementalMatcherCache &matcherCache, bool retained = false
	) {
		const Json &entries = value.at("source_match_units").at(path);
		const Json *oldEntries = priorUnits && priorUnits->at("source_match_units").contains(path)
									 ? &priorUnits->at("source_match_units").at(path)
									 : nullptr;
		std::string results;
		if (retained)
			results = priorUnits->at("source_match_results").at(path).get<std::string>();
		else if (expanded.at("match_entries").contains(path) && expanded.at("match_entries").at(path) == entries)
			results = expanded.at("matches").at(path).get<std::string>();
		else {
			const Json *oldResults = oldEntries ? &priorUnits->at("source_match_results").at(path) : nullptr;
			results = resolvedReferenceMatches(references, paths, matcherCache, oldEntries, oldResults, nullptr).fingerprint;
		}
		// Keep ordered per-reference identities available for a later edit, while
		// consumers depend only on their settled semantic result fingerprint.
		if (matcherCache.contains(results))
			matcherCache.retain(results);
		value.at("source_match_results")[path] = std::move(results);
	}

	void captureBodyUnits(
		ParseContext &context, IncrementalMatcherCache &matcherCache, const std::map<std::string, Section *> &sections
	) {
		if (unitsCaptured)
			return;
		auto timer = measure(&MatcherProjectionWork::initialBodies);
		std::map<const Section *, std::string> paths;
		for (const auto &[path, section] : sections)
			paths.emplace(section, path);
		for (Owner &owner : owners) {
			if (owner.references.empty() && !context.retainedSourceOwners.contains(owner.section))
				owner.references = sourceOwnerReferences(*owner.section);
			if (!owner.topLevel)
				continue;
			if (context.retainedSourceOwners.contains(owner.section) ||
				(context.deferredBodyResolution && context.deferredBodyResolution(owner.section))) {
				value.at("body_units")[owner.path] = priorUnits->at("body_units").at(owner.path);
				value.at("initial_header_definitions")[owner.path] =
					priorUnits->at("initial_header_definitions").at(owner.path);
				matcherCache.retain(value.at("body_units").at(owner.path).get_ref<const std::string &>());
				continue;
			}
			std::vector<std::pair<size_t, PatternReference *>> references;
			for (size_t index = 0; index < owner.references.size(); index++)
				references.emplace_back(index, owner.references[index]);
			if (auto key = matcherCache.recordBodyResolution(references)) {
				value.at("body_units")[owner.path] = std::move(*key);
				if (owner.demandEligible)
					value.at("initial_header_definitions")[owner.path] = headerDefinitions(owner, paths);
			}
		}
		unitsCaptured = true;
	}

	void captureConstraintSources(const Owner &owner, const ParseContext &context) {
		if (!owner.demandEligible)
			return;
		auto timer = measure(&MatcherProjectionWork::constraintSources);
		bool declared = false;
		if (context.retainedSourceOwners.contains(owner.section) ||
			(context.deferredBodyResolution && context.deferredBodyResolution(owner.section))) {
			declared = priorUnits->at("constraint_sources").contains(owner.path);
			if (work)
				work->constraintSourceUnitsReused++;
		} else {
			if (work)
				work->constraintSourceUnitsScanned++;
			auto visit = [&](auto &&self, const Section &scope) -> bool {
				for (const auto &[_, references] : scope.variableReferences)
					for (const VariableReference *reference : references) {
						if (work)
							work->constraintSourceReferencesScanned++;
						if (!reference->declaredTypeConstraintName.empty())
							return true;
					}
				for (const Section *child : scope.children)
					if (child->patternDefinitions.empty() && self(self, *child))
						return true;
				return false;
			};
			declared = visit(visit, *owner.section);
		}
		if (declared) {
			value.at("constraint_sources")[owner.path] = true;
			constraintSourceOwners.insert(owner.section);
		}
	}

	Json finish(
		const ParseContext &context, const Json &expanded, IncrementalMatcherCache &matcherCache,
		const std::map<std::string, Section *> &sections
	) {
		auto timer = measure(&MatcherProjectionWork::final);
		requireCompilerInvariant(value.is_object(), "successful frontend has no initial matcher projection");
		requireCompilerInvariant(unitsCaptured, "successful frontend has no initial body-resolution boundary");
		for (const char *field : {"matches", "match_entries"})
			for (const auto &[_, entry] : expanded.at(field).items()) {
				const std::string &key = entry.get_ref<const std::string &>();
				if (matcherCache.contains(key))
					matcherCache.retain(key);
			}
		std::map<const Section *, std::string> paths;
		for (const auto &[path, section] : sections)
			paths.emplace(section, path);
		for (Owner &owner : owners) {
			captureConstraintSources(owner, context);
			if (owner.references.empty() && !context.retainedSourceOwners.contains(owner.section))
				owner.references = sourceOwnerReferences(*owner.section);
			if (context.retainedSourceOwners.contains(owner.section) ||
				(context.deferredBodyResolution && context.deferredBodyResolution(owner.section))) {
				value.at("source_match_units")[owner.path] = priorUnits->at("source_match_units").at(owner.path);
				captureSourceMatchResults(owner.path, owner.references, expanded, paths, matcherCache, true);
				for (const char *field : {"final_body_units", "header_definitions"})
					value.at(field)[owner.path] = priorUnits->at(field).at(owner.path);
				matcherCache.retain(value.at("final_body_units").at(owner.path).get_ref<const std::string &>());
				if (priorUnits->at("match_overrides").contains(owner.path))
					value.at("match_overrides")[owner.path] = priorUnits->at("match_overrides").at(owner.path);
				continue;
			}
			if (owner.demandEligible) {
				std::vector<std::pair<size_t, PatternReference *>> references;
				for (size_t index = 0; index < owner.references.size(); index++)
					references.emplace_back(index, owner.references[index]);
				if (auto key = matcherCache.recordBodyResolution(references, false)) {
					value.at("final_body_units")[owner.path] = std::move(*key);
					value.at("header_definitions")[owner.path] = headerDefinitions(owner, paths);
				}
			}
			// Most bodies retain their references through expansion. Store only
			// overrides for bodies whose earlier matcher boundary differs.
			const Json &finalEntries = expanded.at("match_entries");
			auto referenceTimer = measure(&MatcherProjectionWork::referenceTables);
			std::string key = matcherCache.recordReferenceKeys(owner.references);
			std::vector<PatternReference *> unitReferences;
			forEachInferenceSourceReference(*owner.section, [&](PatternReference *reference) {
				unitReferences.push_back(reference);
			});
			orderSourceReferences(unitReferences);
			value.at("source_match_units")[owner.path] =
				unitReferences == owner.references ? key : matcherCache.recordReferenceKeys(unitReferences);
			captureSourceMatchResults(owner.path, unitReferences, expanded, paths, matcherCache);
			referenceTimer.stop();
			if (!finalEntries.contains(owner.path) || finalEntries.at(owner.path).get_ref<const std::string &>() != key)
				value.at("match_overrides")[owner.path] = std::move(key);
		}
		auto referenceTimer = measure(&MatcherProjectionWork::referenceTables);
		for (const auto &[path, section] : sections) {
			if ((section->parent && section->patternDefinitions.empty() && !hasCodegenSectionIdentity(*section)) ||
				value.at("source_match_units").contains(path))
				continue;
			std::vector<PatternReference *> references;
			forEachInferenceSourceReference(*section, [&](PatternReference *reference) {
				references.push_back(reference);
			});
			if (!references.empty()) {
				orderSourceReferences(references);
				value.at("source_match_units")[path] = matcherCache.recordReferenceKeys(references);
				captureSourceMatchResults(path, references, expanded, paths, matcherCache);
			}
		}
		return std::move(value);
	}
};
