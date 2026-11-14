// Preserve the matcher boundary before receiver bodies become generated functions.
// Later projections describe expanded sections and cannot identify this stage.
struct InitialMatcherProjection {
	struct Owner {
		std::string path;
		const Section *section;
		bool topLevel;
		std::vector<PatternReference *> references;
		std::vector<std::pair<size_t, PatternReference *>> deferredReferences;
	};
	Json value = nullptr;
	std::vector<Owner> owners;
	bool unitsCaptured = false;
	const Json *priorUnits = nullptr, *published = nullptr;
	const Json &projection() const { return published ? *published : value; }
	size_t declarationsReused = 0;

	void capture(
		const ParseContext &context, const Json &observations, const std::list<PatternReference *> &deferredReferences,
		const Json *previous, const Json &bodyDigests, const std::map<std::string, Section *> &sections, std::string_view tree
	) {
		const Json *prior = previous ? &previous->at("matcher_projection") : nullptr;
		priorUnits = prior;
		declarationsReused = 0;
		owners.clear();
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
			Owner owner{path, section, section->parent == context.mainSection, {}, {}};
			const auto collect = [&](auto &&self, Section *body) -> void {
				owner.references.insert(owner.references.end(), body->patternReferences.begin(), body->patternReferences.end());
				for (Section *child : body->children)
					if (child->patternDefinitions.empty())
						self(self, child);
			};
			collect(collect, section);
			if (owner.references.empty())
				continue;
			for (size_t index = 0; index < owner.references.size(); index++)
				if (deferred.contains(owner.references[index]))
					owner.deferredReferences.emplace_back(index, owner.references[index]);
			owners.push_back(std::move(owner));
		}
		value = Json{
			{"selected_digest", digest(selected.dump())},
			{"declaration_digests", Json::object()},
			{"match_overrides", Json::object()},
			{"body_units", Json::object()},
			{"final_body_units", Json::object()},
			{"header_definitions", Json::object()},
			{"initial_header_definitions", Json::object()},
			{"bodies", bodyDigests},
			{"scope_rule_bodies", scopeRuleBodyPaths(sections)},
			{"global_resolution", globalResolutionIdentity(*context.mainSection)},
			{"sources", sourceProjection(
							context, observations, functionBodies, false, false, false, prior ? &prior->at("sources") : nullptr
						)},
			{"ambient_sources",
			 sourceProjection(
				 context, observations, reusable, true, true, false, prior ? &prior->at("ambient_sources") : nullptr
			 )},
			{"ambient_nonexecution_sources",
			 sourceProjection(
				 context, observations, reusable, true, true, true, prior ? &prior->at("ambient_nonexecution_sources") : nullptr
			 )}
		};
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
			if (stableDeclarations && prior->at("declaration_digests").contains(path) && prior->at("bodies").contains(path) &&
				bodyDigests.at(path) == prior->at("bodies").at(path)) {
				value.at("declaration_digests")[path] = prior->at("declaration_digests").at(path);
				declarationsReused++;
			} else
				value.at("declaration_digests")[path] = digest(sectionDeclaration(*owner.section).dump());
		}
	}

	static Json headerDefinitions(const Owner &owner, const std::map<const Section *, std::string> &paths) {
		Json headers = Json::array();
		for (size_t index = 0; index < owner.references.size(); index++) {
			PatternReference *reference = owner.references[index];
			if (!reference->range().line->sectionOpening || !reference->match)
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
			headers.push_back(Json::array({index, std::move(definitions)}));
		}
		return headers;
	}

	void captureBodyUnits(ParseContext &context, IncrementalMatcherCache &matcherCache) {
		if (unitsCaptured)
			return;
		std::map<std::string, Section *> scopes;
		std::map<const Section *, std::string> paths;
		collectSections(context.mainSection, "root", scopes);
		for (const auto &[path, section] : scopes)
			paths.emplace(section, path);
		for (const Owner &owner : owners) {
			if (!owner.topLevel)
				continue;
			if (context.deferredBodyResolution && context.deferredBodyResolution(owner.section)) {
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
				value.at("initial_header_definitions")[owner.path] = headerDefinitions(owner, paths);
			}
		}
		unitsCaptured = true;
	}

	Json finish(const ParseContext &context, const Json &expanded, IncrementalMatcherCache &matcherCache) {
		requireCompilerInvariant(value.is_object(), "successful frontend has no initial matcher projection");
		requireCompilerInvariant(unitsCaptured, "successful frontend has no initial body-resolution boundary");
		for (const char *field : {"matches", "match_entries"})
			for (const auto &[_, entry] : expanded.at(field).items()) {
				const std::string &key = entry.get_ref<const std::string &>();
				if (matcherCache.contains(key))
					matcherCache.retain(key);
			}
		std::map<const Section *, std::string> paths;
		std::map<std::string, Section *> scopes;
		collectSections(context.mainSection, "root", scopes);
		for (const auto &[path, section] : scopes)
			paths.emplace(section, path);
		for (const Owner &owner : owners) {
			if (context.deferredBodyResolution && context.deferredBodyResolution(owner.section)) {
				for (const char *field : {"final_body_units", "header_definitions"})
					value.at(field)[owner.path] = priorUnits->at(field).at(owner.path);
				matcherCache.retain(value.at("final_body_units").at(owner.path).get_ref<const std::string &>());
				if (priorUnits->at("match_overrides").contains(owner.path))
					value.at("match_overrides")[owner.path] = priorUnits->at("match_overrides").at(owner.path);
				continue;
			}
			std::vector<std::pair<size_t, PatternReference *>> references;
			for (size_t index = 0; index < owner.references.size(); index++)
				references.emplace_back(index, owner.references[index]);
			Json headers = headerDefinitions(owner, paths);
			if (owner.topLevel)
				if (auto key = matcherCache.recordBodyResolution(references, false)) {
					value.at("final_body_units")[owner.path] = std::move(*key);
					value.at("header_definitions")[owner.path] = std::move(headers);
				}
			// Most bodies retain their references through expansion. Store only
			// overrides for bodies whose earlier matcher boundary differs.
			const Json &finalEntries = expanded.at("match_entries");
			std::string key = matcherCache.recordReferenceKeys(owner.references);
			if (!finalEntries.contains(owner.path) || finalEntries.at(owner.path).get_ref<const std::string &>() != key)
				value.at("match_overrides")[owner.path] = std::move(key);
		}
		return std::move(value);
	}
};
