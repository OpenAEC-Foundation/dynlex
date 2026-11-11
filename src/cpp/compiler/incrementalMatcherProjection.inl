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
	size_t declarationsReused = 0;

	void capture(
		const ParseContext &context, const Json &observations, const std::list<PatternReference *> &deferredReferences,
		const Json *previous, const Json &bodyDigests, const std::map<std::string, Section *> &sections, std::string_view tree,
		const Json &parameterCounts
	) {
		const Json *prior = previous ? &previous->at("matcher_projection") : nullptr;
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
			{"body_promoted_parameters", Json::object()},
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
			if (parameterCounts.contains(path)) {
				bool bodyPromoted = false;
				for (PatternDefinition *definition : owner.section->patternDefinitions)
					forEachLeafElement(definition->patternElements, [&](const DefinitionPatternElement &element) {
						if (element.promotedFromVariableLike && parameterCounts.at(path).contains(element.text))
							bodyPromoted = true;
					});
				if (bodyPromoted)
					value.at("body_promoted_parameters")[path] = true;
			}

			if (stableDeclarations && prior->at("declaration_digests").contains(path) && prior->at("bodies").contains(path) &&
				bodyDigests.at(path) == prior->at("bodies").at(path)) {
				value.at("declaration_digests")[path] = prior->at("declaration_digests").at(path);
				declarationsReused++;
			} else
				value.at("declaration_digests")[path] = digest(sectionDeclaration(*owner.section).dump());
		}
	}

	void captureBodyUnits(IncrementalMatcherCache &matcherCache) {
		if (unitsCaptured)
			return;
		for (const Owner &owner : owners) {
			if (!owner.topLevel)
				continue;
			std::vector<std::pair<size_t, PatternReference *>> references;
			for (size_t index = 0; index < owner.references.size(); index++)
				references.emplace_back(index, owner.references[index]);
			if (auto key = matcherCache.recordBodyResolution(references))
				value.at("body_units")[owner.path] = std::move(*key);
		}
		unitsCaptured = true;
	}

	Json finish(const Json &expanded, IncrementalMatcherCache &matcherCache) {
		requireCompilerInvariant(value.is_object(), "successful frontend has no initial matcher projection");
		requireCompilerInvariant(unitsCaptured, "successful frontend has no initial body-resolution boundary");
		for (const char *field : {"matches", "match_entries"})
			for (const auto &[_, entry] : expanded.at(field).items()) {
				const std::string &key = entry.get_ref<const std::string &>();
				if (matcherCache.contains(key))
					matcherCache.retain(key);
			}
		for (const Owner &owner : owners) {
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
