// Preserve the matcher boundary before receiver bodies become generated functions.
// Later projections describe expanded sections and cannot identify this stage.
struct InitialMatcherProjection {
	struct Owner {
		std::string path;
		std::vector<PatternReference *> references;
		const Json *skipped;
	};
	Json value = nullptr;
	std::vector<Owner> owners;

	void capture(
		const ParseContext &context, const Json &observations, const std::unordered_map<const Section *, const Json *> &skipped
	) {
		owners.clear();
		std::map<std::string, Section *> sections;
		collectSections(context.mainSection, "root", sections);
		std::unordered_set<const Section *> functionBodies, reusable;
		Json selected = Json::object(), declarations = Json::object();
		for (const auto &[path, section] : sections) {
			if (section->type == SectionType::Function && !section->patternDefinitions.empty() &&
				!containsNestedDefinition(*section)) {
				selected[path] = sectionIdentity(*section);
				functionBodies.insert(section);
			}
			if (!reusableCodegenSection(*section))
				continue;
			reusable.insert(section);
			declarations[path] = digest(sectionDeclaration(*section).dump());
			Owner owner{path, {}, skipped.contains(section) ? skipped.at(section) : nullptr};
			const auto collect = [&](auto &&self, Section *body) -> void {
				owner.references.insert(owner.references.end(), body->patternReferences.begin(), body->patternReferences.end());
				for (Section *child : body->children)
					if (child->patternDefinitions.empty())
						self(self, child);
			};
			collect(collect, section);
			owners.push_back(std::move(owner));
		}
		value = Json{
			{"selected_digest", digest(selected.dump())},
			{"declaration_digests", std::move(declarations)},
			{"match_overrides", Json::object()},
			{"bodies", functionBodyDigests(context, sections)},
			{"scope_rule_bodies", scopeRuleBodyPaths(sections)},
			{"global_resolution", globalResolutionIdentity(*context.mainSection)},
			{"sources", sourceProjection(context, observations, functionBodies)},
			{"ambient_sources", sourceProjection(context, observations, reusable, true, true)},
			{"ambient_nonexecution_sources", sourceProjection(context, observations, reusable, true, true, true)}
		};
	}

	Json finish(const Json &expanded) {
		requireCompilerInvariant(value.is_object(), "successful frontend has no initial matcher projection");
		auto sameKey = [](const Json &saved, const std::string &key) {
			return saved.is_null() ? key.empty() : saved.get_ref<const std::string &>() == key;
		};
		for (const Owner &owner : owners) {
			// Most bodies retain their references through expansion. Store only
			// overrides for bodies whose earlier matcher boundary differs.
			const Json &finalEntries = expanded.at("match_entries");
			bool identical = finalEntries.contains(owner.path) && finalEntries.at(owner.path).size() == owner.references.size();
			for (size_t index = 0; identical && index < owner.references.size(); index++) {
				const Json &saved = finalEntries.at(owner.path).at(index);
				if (owner.skipped) {
					for (const char *field : {"shape", "key", "initial_key"})
						identical &= owner.skipped->at(index).at(field) == saved.at(field);
					continue;
				}
				const PatternReference &reference = *owner.references[index];
				const Json &shape = saved.at("shape");
				identical = shape.at(0).get_ref<const std::string &>() == reference.pattern.text &&
							shape.at(1).get<int>() == static_cast<int>(reference.patternType) &&
							shape.at(2).get<int>() == static_cast<int>(reference.purpose) &&
							sameKey(saved.at("key"), reference.incrementalMatcherKey) &&
							sameKey(saved.at("initial_key"), reference.initialMatcherKey);
			}
			if (identical)
				continue;
			Json entries = owner.skipped ? *owner.skipped : Json::array();
			if (!owner.skipped)
				for (const PatternReference *reference : owner.references)
					entries.push_back(
						Json{
							{"shape", Json::array(
										  {reference->pattern.text, static_cast<int>(reference->patternType),
										   static_cast<int>(reference->purpose)}
									  )},
							{"key",
							 reference->incrementalMatcherKey.empty() ? Json(nullptr) : Json(reference->incrementalMatcherKey)},
							{"initial_key",
							 reference->initialMatcherKey.empty() ? Json(nullptr) : Json(reference->initialMatcherKey)}
						}
					);
			value.at("match_overrides")[owner.path] = std::move(entries);
		}
		return std::move(value);
	}
};
