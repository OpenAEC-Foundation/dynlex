// A whole result remains encoded until a semantic consumer needs its matches.
// The ordinary resolver still owns changed units and all unproved inputs.
class IncrementalBodyDemand {
	ParseContext &context;
	IncrementalMatcherCache &cache;
	const Json *prior;
	MatcherPathSnapshots &paths;
	InitialParameterCounts &counts;
	std::unordered_map<const Section *, BodyResolutionUnit> pending;
	std::array<std::unordered_map<const PatternReference *, std::vector<PatternDefinition *>>, 2> headers;
	std::optional<MatcherTreeChanges> finalChanges;
	bool checked = false, stableRules = false;

	const Section *owner(const Section *scope) const {
		for (; scope; scope = scope->parent)
			if (pending.contains(scope))
				return scope;
		return nullptr;
	}
	bool select(BodyResolutionUnit &unit, std::string_view tree) {
		Section *section = unit.section;
		if (!prior || context.options.emitDebugInfo || context.options.traceExecution ||
			section->type != SectionType::Function || section->isFlex || section->parent != context.mainSection ||
			!hasNoGlobalAccess(section) || unit.deferredIndices.size() != unit.references.size() ||
			std::ranges::any_of(unit.references, [](const PatternReference *reference) {
			return !reference->expression.deferred() || reference->expression.materialized() != nullptr;
		}))
			return false;
		if (!checked) {
			stableRules =
				precedenceRulesSignature(*context.mainSection) == prior->at("precedence_rules").get_ref<const std::string &>();
			finalChanges = paths.compare(prior->at("final_matcher_tree").get_ref<const std::string &>(), tree);
			checked = true;
		}
		const Json &saved = prior->at("matcher_projection");
		if (!stableRules || (finalChanges && finalChanges->mayAffect(*section)) ||
			!saved.at("final_body_units").contains(unit.path) ||
			!cache.contains(saved.at("final_body_units").at(unit.path).get_ref<const std::string &>()))
			return false;
		const auto *entries = savedReferenceKeys(*prior, cache, unit.path);
		requireCompilerInvariant(
			entries && entries->size() == unit.references.size(), "deferred body lost its reference metadata"
		);
		const std::string &final = saved.at("final_body_units").at(unit.path).get_ref<const std::string &>();
		if (!cache.retainBodyResolutionDependencies(unit.key) ||
			(final != unit.key && !cache.retainBodyResolutionDependencies(final)))
			return false;
		const auto &scopes = counts.sections();
		for (size_t stage = 0; stage < 2; stage++) {
			const char *field = stage ? "header_definitions" : "initial_header_definitions";
			for (const Json &header : saved.at(field).at(unit.path)) {
				auto &definitions = headers[stage][unit.references.at(header.at(0).get<size_t>())];
				for (const Json &encoded : header.at(1)) {
					Section *provider = scopes.at(encoded.at(0).get<std::string>());
					size_t occurrence = encoded.at(2).get<size_t>();
					PatternDefinition *found = nullptr;
					for (PatternDefinition *definition : provider->patternDefinitions)
						if (definition->toString() == encoded.at(1).get_ref<const std::string &>() && occurrence-- == 0) {
							found = definition;
							break;
						}
					requireCompilerInvariant(found, "deferred header lost a validated definition");
					definitions.push_back(found);
				}
			}
		}

		for (size_t index = 0; index < unit.references.size(); index++) {
			PatternReference *reference = unit.references[index];
			reference->incrementalMatcherKey = entries->at(index).initialKey;
			reference->resolve();
		}
		cache.retain(unit.key);
		cache.retain(saved.at("final_body_units").at(unit.path).get_ref<const std::string &>());
		context.bodyResolutionUnitsDeferred++;
		context.bodyResolutionUnitsReused++;
		pending.emplace(section, std::move(unit));
		return true;
	}

  public:
	IncrementalBodyDemand(
		ParseContext &context, IncrementalMatcherCache &cache, const Json *previous, MatcherPathSnapshots &paths,
		InitialParameterCounts &counts
	)
		: context(context), cache(cache),
		  prior(previous && previous->contains("all_function_projection") ? &previous->at("all_function_projection") : nullptr),
		  paths(paths), counts(counts) {
		context.deferredBodyResolution = [&](const Section *scope) {
			return owner(scope) != nullptr;
		};
		context.demandBodyResolution = [&](Section *scope) {
			demand(scope);
		};
		context.deferredHeaderDefinitions = [&](PatternReference *reference) {
			const auto &stage = headers[context.patternPrecedenceRevision != 0];
			auto found = stage.find(reference);
			return found == stage.end() ? std::vector<PatternDefinition *>{} : found->second;
		};
		context.demandBodiesForDefinition = [&](PatternDefinition &definition) {
			std::vector<Section *> affected;
			for (const auto &[section, unit] : pending)
				if (std::ranges::any_of(unit.references, [&](const PatternReference *reference) {
					return isPatternDefinitionVisibleFromSource(definition, *reference->range().line->sourceFile) &&
						   definitionCouldMatchReference(definition, *reference);
				}))
					affected.push_back(const_cast<Section *>(section));
			for (Section *section : affected)
				demand(section);
		};
	}
	~IncrementalBodyDemand() {
		context.deferredBodyResolution = {};
		context.demandBodyResolution = {};
		context.deferredHeaderDefinitions = {};
		context.demandBodiesForDefinition = {};
	}
	std::vector<ParseContext::RestoredBodyReference> restore(std::vector<BodyResolutionUnit> &units, std::string_view tree) {
		std::erase_if(units, [&](BodyResolutionUnit &unit) {
			return select(unit, tree);
		});
		return restoreBodyResolutionUnits(context, cache, units);
	}
	void demand(Section *scope) {
		const Section *selected = owner(scope);
		if (!selected)
			return;
		BodyResolutionUnit unit = std::move(pending.at(selected));
		pending.erase(selected);
		context.bodyResolutionUnitsDeferred--;
		context.bodyResolutionUnitsDemanded++;
		bool final = context.patternPrecedenceRevision != 0 || context.sourceSyntaxResolutionComplete;
		const std::string key =
			final ? prior->at("matcher_projection").at("final_body_units").at(unit.path).get<std::string>() : unit.key;
		auto records = cache.restoreBodyResolution(key);
		requireCompilerInvariant(
			records && records->size() == unit.references.size(), "validated deferred body lost its result"
		);
		for (size_t index = 0; index < unit.references.size(); index++) {
			auto &saved = records->at(index);
			PatternReference *reference = unit.references[index];
			ParseContext::RestoredBodyReference result{reference, {}, {}};
			if (saved.variable) {
				reference->incrementalMatcherKey = saved.matchKey;
				reference->prevalidatedMatcherKey.clear();
				if (!saved.matchKey.empty())
					cache.retain(saved.matchKey);
				result.variable = std::move(saved.variable);
			} else {
				result.match.reset(cache.restoreBodyMatch(*reference, saved.matchKey));
			}
			context.restoreBodyReference(std::move(result));
		}
		if (context.sourceSyntaxResolutionComplete) {
			materializeVariableGroups(context, unit.section);
			for (const auto &[observer, publish] : context.bodyBindingObservers) {
				(void)observer;
				publish(*unit.section);
			}
		}
	}
};
