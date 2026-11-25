// A whole result remains encoded until a semantic consumer needs its matches.
// The ordinary resolver still owns changed units and all unproved inputs.
class IncrementalBodyDemand {
	ParseContext &context;
	IncrementalMatcherCache &cache;
	const Json *prior;
	MatcherPathSnapshots &paths;
	InitialParameterCounts &counts;
	std::unordered_map<const Section *, BodyResolutionUnit> pending;
	std::array<std::unordered_map<const CodeLine *, std::vector<PatternDefinition *>>, 2> headers;
	std::optional<MatcherTreeChanges> finalChanges;
	bool checked = false, stableRules = false;
	bool sourceIndexReady = false;
	std::unordered_map<std::string, const lsp::SourceFile *> sourceIndex;

	const Section *owner(const Section *scope) const {
		for (; scope; scope = scope->parent)
			if (pending.contains(scope))
				return scope;
		return nullptr;
	}
	bool select(BodyResolutionUnit &unit, std::string_view tree) {
		Section *section = unit.section;
		if (!prior || context.options.traceExecution || context.options.debugExecution ||
			section->type != SectionType::Function || section->isFlex || section->parent != context.mainSection ||
			!hasNoGlobalAccess(section) ||
			(!context.retainedSourceOwners.contains(section) && unit.deferredIndices.size() != unit.references.size()) ||
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
		if (!stableRules ||
			(finalChanges &&
			 finalChanges->mayAffect(*section, &prior->at("matcher_projection").at("numeric_spellings").at(unit.path))) ||
			!saved.at("final_body_units").contains(unit.path) ||
			!cache.contains(saved.at("final_body_units").at(unit.path).get_ref<const std::string &>()))
			return false;
		const auto *entries = savedReferenceKeys(*prior, cache, unit.path);
		requireCompilerInvariant(
			entries && entries->size() == unit.referenceCount, "deferred body lost its reference metadata"
		);
		const std::string &final = saved.at("final_body_units").at(unit.path).get_ref<const std::string &>();
		if (!cache.retainBodyResolutionDependencies(unit.key) ||
			(final != unit.key && !cache.retainBodyResolutionDependencies(final)))
			return false;
		if (context.retainedSourceOwners.contains(section)) {
			if (!sourceIndexReady) {
				for (const auto &[_, source] : context.importedFiles)
					sourceIndex.emplace(source->uri, source);
				sourceIndexReady = true;
			}
			const Json &sources = saved.at("reference_sources").at(unit.path);
			requireCompilerInvariant(!sources.empty() && sources.at(0).at(0) == 0, "retained owner has no reference source");
			size_t range = 0;
			for (size_t index = 0; index < unit.referenceCount; index++) {
				if (range + 1 < sources.size() && sources.at(range + 1).at(0) == index)
					range++;
				unit.referenceSources.push_back(sourceIndex.at(sources.at(range).at(1).get<std::string>()));
			}
		}
		const auto &scopes = counts.sections();
		for (size_t stage = 0; stage < 2; stage++) {
			const char *field = stage ? "header_definitions" : "initial_header_definitions";
			for (const Json &header : saved.at(field).at(unit.path)) {
				auto &definitions = headers[stage][unit.lines.at(header.at(0).get<size_t>())];
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
		context.deferredHeaderDefinitions = [&](CodeLine *line) -> std::optional<std::vector<PatternDefinition *>> {
			if (!owner(line->section))
				return std::nullopt;
			const auto &stage = headers[context.patternPrecedenceRevision != 0];
			auto found = stage.find(line);
			return found == stage.end() ? std::vector<PatternDefinition *>{} : found->second;
		};
		context.demandBodiesForDefinition = [&](PatternDefinition &definition) {
			std::vector<Section *> affected;
			for (auto &[section, unit] : pending) {
				bool matches = false;
				if (context.retainedSourceOwners.contains(section)) {
					const auto *references = savedReferenceKeys(*prior, cache, unit.path);
					requireCompilerInvariant(references, "retained owner lost reference identities");
					if (unit.referenceTokens.empty())
						for (const auto &reference : *references)
							unit.referenceTokens.push_back(getPatternElements(reference.pattern));
					const Json &numbers = prior->at("matcher_projection").at("numeric_spellings").at(unit.path);
					for (size_t index = 0; index < references->size() && !matches; index++) {
						if (!isPatternDefinitionVisibleFromSource(definition, *unit.referenceSources.at(index)))
							continue;
						const std::string key = std::to_string(index);
						for (const auto &path : definition.indexedPaths)
							if (std::ranges::all_of(path, [&](const PatternElement &required) {
								if (required.type != PatternElement::Type::VariableLike)
									return true;
								if (std::ranges::any_of(unit.referenceTokens.at(index), [&](const PatternElement &element) {
									return element.text == required.text;
								}))
									return true;
								return numbers.contains(key) && std::ranges::any_of(numbers.at(key), [&](const Json &spelling) {
									return spelling == required.text;
								});
							})) {
								matches = true;
								break;
							}
					}
				} else
					matches = std::ranges::any_of(unit.references, [&](const PatternReference *reference) {
						return isPatternDefinitionVisibleFromSource(definition, *reference->range().line->sourceFile) &&
							   definitionCouldMatchReference(definition, *reference);
					});
				if (matches)
					affected.push_back(const_cast<Section *>(section));
			}
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
	std::vector<ParseContext::RestoredBodyReference>
	restore(std::vector<BodyResolutionUnit> &units, std::string_view tree, std::list<PatternReference *> &references) {
		std::erase_if(units, [&](BodyResolutionUnit &unit) {
			if (select(unit, tree))
				return true;
			if (context.retainedSourceOwners.contains(unit.section)) {
				context.materializeSourceOwnerReferences(unit.section);
				unit.references = sourceOwnerReferences(*unit.section);
				references.insert(references.end(), unit.references.begin(), unit.references.end());
				return true; // These references are unresolved and use the normal matcher.
			}
			return false;
		});
		return restoreBodyResolutionUnits(context, cache, units);
	}
	void demand(Section *scope) {
		const Section *selected = owner(scope);
		if (!selected)
			return;
		BodyResolutionUnit unit = std::move(pending.at(selected));
		pending.erase(selected);
		if (context.retainedSourceOwners.contains(selected)) {
			context.materializeSourceOwnerReferences(const_cast<Section *>(selected));
			unit.references = sourceOwnerReferences(*selected);
			requireCompilerInvariant(unit.references.size() == unit.referenceCount, "source owner reference domain changed");
		}
		context.bodyResolutionUnitsDeferred--;
		context.bodyResolutionUnitsDemanded++;
		bool final = context.patternPrecedenceRevision != 0 || context.sourceSyntaxResolutionComplete;
		const std::string key =
			final ? prior->at("matcher_projection").at("final_body_units").at(unit.path).get<std::string>() : unit.key;
		auto records = cache.restoreBodyResolution(key);
		requireCompilerInvariant(records && records->size() == unit.referenceCount, "validated deferred body lost its result");
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
