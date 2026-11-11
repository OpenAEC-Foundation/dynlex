struct ClassificationWork {
	size_t unitsReused = 0;
	size_t parametersReused = 0;
	size_t parametersRecorded = 0;
	size_t unitsInvalidated = 0;
	size_t queryUnitsReused = 0;
	size_t queryUnitsBuilt = 0;
	size_t dependencyUnitsReused = 0;
	size_t edges = 0;
	size_t candidateChecks = 0;
	double milliseconds = 0;
	double loadMilliseconds = 0, inputMilliseconds = 0, environmentMilliseconds = 0, scopeMilliseconds = 0;
	double dependencyMilliseconds = 0, restoreMilliseconds = 0, saveMilliseconds = 0;
};

class IncrementalClassification {
	enum Field { Path, Input, Queries, Provides, Outputs, Edges, HasCandidates, Environment };
	struct Unit {
		std::string path, input, queries, provides, outputs;
		Section *section;
		const Json *old = nullptr;
		bool hasCandidates = false;
		bool environment = false;
		bool selected = false;
		std::vector<PatternReference *> references;
		std::vector<CodeLine *> lines;
		std::vector<size_t> edges;
	};
	struct Timer {
		ClassificationWork &work;
		double *stage = nullptr;
		Clock::time_point started = Clock::now();
		~Timer() {
			double elapsed = elapsedMilliseconds(started);
			work.milliseconds += elapsed;
			if (stage)
				*stage += elapsed;
		}
	};

  public:
	IncrementalClassification(
		ParseContext &context, IncrementalMatcherCache &cache, const Json *previous, const Json &observations,
		InitialParameterCounts &counts, ClassificationWork &work
	)
		: context(context), cache(cache), previous(previous), observations(observations), counts(counts), work(work) {}

	std::vector<Section *> select() {
		Timer timer{work};
		prepare();
		std::vector<Section *> result;
		if (!prior || context.options.emitDebugInfo || context.options.traceExecution)
			return result;
		auto restoreStarted = Clock::now();
		const Json &projection = previous->at("all_function_projection");
		const Json &matcher = projection.at("matcher_projection");
		std::set<std::string> diagnostics = previous->at("diagnostic_sources").get<std::set<std::string>>();
		for (Unit &unit : units) {
			Section &section = *unit.section;
			if (unit.environment || dirty.contains(unit.path) || !unit.old || section.parent != context.mainSection ||
				!reusableResolutionSection(section) || unit.references.empty() ||
				diagnostics.contains(resolutionSourceFile(section).uri) || !sameBody(unit.path) ||
				!matcher.at("body_units").contains(unit.path))
				continue;
			auto references = savedReferenceKeys(projection, cache, unit.path);
			if (!references || references->size() != unit.references.size())
				continue;
			bool available = cache.contains(matcher.at("body_units").at(unit.path).get_ref<const std::string &>());
			for (const auto &reference : *references)
				for (std::string_view key : {reference.initialKey, reference.finalKey})
					available &= key.empty() || cache.contains(key);
			if (!available)
				continue;
			const Json &outputs = data(unit.old->at(Outputs).get_ref<const std::string &>());
			std::map<std::string, Range> bindings;
			for (const Json &output : outputs) {
				PatternDefinition *definition = section.patternDefinitions.at(output.at(0).get<size_t>());
				size_t wanted = output.at(1).get<size_t>(), index = 0;
				Range use(unit.lines.at(output.at(2).get<size_t>()), output.at(3).get<int>(), output.at(4).get<int>());
				forEachLeafElement(definition->patternElements, [&](DefinitionPatternElement &element) {
					if (index++ != wanted)
						return;
					promoteImplicitPatternParameter(context, *definition, element, use);
					bindings.emplace(element.text, use);
				});
			}
			for (const auto &[name, use] : bindings)
				requireCompilerInvariant(
					section.resolvePatternParameterBinding(context, name, use), "cached parameter has no binding"
				);
			unit.selected = true;
			unit.outputs = unit.old->at(Outputs).get<std::string>();
			work.unitsReused++;
			work.parametersReused += outputs.size();
			result.push_back(&section);
		}
		work.restoreMilliseconds += elapsedMilliseconds(restoreStarted);
		return result;
	}

	void recordOutputs() {
		Timer timer{work, &work.saveMilliseconds};
		requireCompilerInvariant(prepared, "classification boundary precedes its inputs");
		work.parametersRecorded = 0;
		for (Unit &unit : units) {
			if (unit.selected) {
				work.parametersRecorded += data(unit.outputs).size();
				continue;
			}
			Json outputs = Json::array();
			if (!unit.environment && unit.section->parent == context.mainSection && reusableResolutionSection(*unit.section))
				for (size_t definitionIndex = 0; definitionIndex < unit.section->patternDefinitions.size(); definitionIndex++) {
					PatternDefinition *definition = unit.section->patternDefinitions[definitionIndex];
					size_t leaf = 0;
					forEachLeafElement(definition->patternElements, [&](const DefinitionPatternElement &element) {
						size_t index = leaf++;
						if (!element.promotedFromVariableLike || !counts.snapshot().contains(unit.path) ||
							!counts.snapshot().at(unit.path).contains(element.text))
							return;
						const Range &use = element.firstImplicitPromotionUseRange;
						auto line = std::ranges::find(unit.lines, use.line);
						requireCompilerInvariant(line != unit.lines.end(), "parameter promotion has no owning source line");
						outputs.push_back(
							Json::array({definitionIndex, index, line - unit.lines.begin(), use.start(), use.end()})
						);
					});
				}
			work.parametersRecorded += outputs.size();
			unit.outputs = save(std::move(outputs));
		}
		captured = true;
	}

	std::string snapshot() {
		Timer timer{work, &work.saveMilliseconds};
		requireCompilerInvariant(captured, "classification snapshot has no resolved boundary");
		Json entries = Json::array();
		for (const Unit &unit : units) {
			for (const std::string *key : {&unit.queries, &unit.provides, &unit.outputs})
				if (cache.contains(*key))
					cache.retain(*key);
				else
					requireCompilerInvariant(known.contains(*key), "classification unit was not recorded");
			entries.push_back(
				Json::array(
					{unit.path, unit.input, unit.queries, unit.provides, unit.outputs, unit.edges, unit.hasCandidates,
					 unit.environment}
				)
			);
		}
		return cache.recordClassificationData(
			Json{{"units", std::move(entries)}, {"sources", std::move(sources)}, {"rules", std::move(rules)}}
		);
	}

  private:
	std::string save(Json value) {
		std::string key = cache.recordClassificationData(value);
		known.try_emplace(key, std::move(value));
		return key;
	}
	const Json &data(const std::string &key) {
		auto found = known.find(key);
		if (found != known.end())
			return found->second;
		auto restored = cache.restoreClassificationData(key);
		requireCompilerInvariant(restored.has_value(), "classification topology lost a referenced unit");
		return known.emplace(key, std::move(*restored)).first->second;
	}
	bool sameBody(const std::string &path) const {
		const Json &before = previous->at("all_function_projection").at("matcher_projection").at("bodies");
		return before.contains(path) && before.at(path) == counts.bodies().at(path);
	}
	static std::string encoded(std::string_view text) { return llvm::toHex(llvm::StringRef(text), true); }
	std::string declarationInput(const Unit &unit) const {
		const Section &section = *unit.section;
		Json definitions = Json::array();
		for (PatternDefinition *definition : section.patternDefinitions) {
			if (definition->hasPrebuiltPatternElements) {
				definitions.push_back(resolvedDefinitionElements(definition->patternElements));
				continue;
			}
			Json promoted = Json::array();
			size_t index = 0;
			forEachLeafElement(definition->patternElements, [&](const DefinitionPatternElement &element) {
				if (element.promotedFromVariableLike)
					promoted.push_back(index);
				index++;
			});
			// The source and configured parser determine every authored element;
			// only explicit body references may have promoted a word at this point.
			definitions.push_back(Json::array({std::string(definition->range.subString), std::move(promoted)}));
		}
		return Json::array({unit.path, std::move(definitions), section.isLocal, section.isFlex, section.isExposed,
							section.isConversion, section.isImplicitConversion, static_cast<int>(section.returnContract),
							section.beforePatterns, section.afterPatterns, static_cast<int>(section.associativity),
							section.dataSection ? Json(section.dataSection->declarationParameter) : Json(nullptr),
							section.dataFieldOwner ? sectionIdentity(*section.dataFieldOwner) : Json(nullptr)})
			.dump();
	}
	Json provided(const Unit &unit) const {
		std::set<std::vector<std::string>> paths;
		for (const PatternDefinition *definition : unit.section->patternDefinitions)
			for (const auto &path : canonicalPatternPaths(definition->patternElements)) {
				std::set<std::string> required;
				for (const DefinitionPatternElement &element : path) {
					if (element.type == PatternElement::Type::Variable ||
						(unit.section->canPromoteImplicitParameter(*definition, element) &&
						 unit.section->variableLikeCounts.at(element.text) != 0))
						continue;
					if (element.type == PatternElement::Type::VariableLike)
						required.insert(encoded(element.text));
					else
						for (size_t index = 0; index < element.text.size(); index++)
							if (!std::isspace(static_cast<unsigned char>(element.text[index])))
								required.insert(encoded(std::string_view(element.text).substr(index, 1)));
				}
				paths.emplace(required.begin(), required.end());
			}
		return paths;
	}
	Json queried(const Unit &unit) const {
		std::unordered_set<std::string_view> literals;
		std::set<std::string> result;
		for (const PatternReference *reference : unit.references) {
			addMatcherStartLiterals(reference->pattern.text, literals);
			addMatcherStartLiterals(reference->range().line->fullText, literals);
			if (reference->expression)
				for (size_t index = 0; index < reference->expression->arguments.size(); index++)
					if (auto spelling = numericPatternArgumentSpelling(reference, index)) {
						std::unordered_set<std::string_view> numeric;
						addMatcherStartLiterals(*spelling, numeric);
						for (std::string_view literal : numeric)
							result.insert(encoded(literal));
					}
		}
		for (std::string_view literal : literals)
			result.insert(encoded(literal));
		return result;
	}
	bool couldReadQueries(const Json &queries, const Unit &reader, const Unit &target) {
		if (reader.references.empty())
			return false;
		work.candidateChecks++;
		const Json &paths = data(target.provides);
		for (const Json &path : paths)
			if (std::ranges::all_of(path, [&](const Json &literal) {
				return std::binary_search(queries.begin(), queries.end(), literal);
			})) {
				if (!target.section->isLocal)
					return true;
				return std::ranges::any_of(reader.references, [&](const PatternReference *reference) {
					return std::ranges::any_of(target.section->patternDefinitions, [&](const PatternDefinition *definition) {
						return isPatternDefinitionVisibleFromSource(*definition, *reference->range().line->sourceFile);
					});
				});
			}
		return false;
	}
	bool couldRead(const Unit &unit, const Unit &target) { return couldReadQueries(data(unit.queries), unit, target); }

	void prepare();
	void prepareEnvironments();
	void dependencies();
	ParseContext &context;
	IncrementalMatcherCache &cache;
	const Json *previous;
	const Json &observations;
	InitialParameterCounts &counts;
	ClassificationWork &work;
	bool prepared = false, captured = false;
	std::optional<Json> prior;
	std::vector<Unit> units;
	std::unordered_map<std::string, size_t> indices;
	std::unordered_map<const Section *, size_t> sectionUnits;
	std::unordered_map<const Section *, size_t> environmentUnits;
	std::unordered_map<std::string, const Json *> oldUnits;
	std::unordered_map<std::string, Json> known;
	std::unordered_set<std::string> dirty;
	Json sources, rules;
};

#include "incrementalClassificationGraph.inl"
#include "incrementalClassificationScopes.inl"
