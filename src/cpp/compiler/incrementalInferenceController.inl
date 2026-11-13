// Inference replay and IR import share concrete-unit eligibility. A rebuilt
// interface stops propagation only for the instantiation that was compared.
class IncrementalInferenceController {
  public:
	IncrementalInferenceController(
		const Json &previous, const Json *nativePrevious, const Json &observations, const Json &projection,
		const std::map<const Section *, std::string> &paths, IncrementalInferenceReads &reads, const Json &frontendDiagnostics,
		BuildStatistics &statistics, std::unique_ptr<IncrementalIRReuse> &irReuse,
		std::unique_ptr<IncrementalInferenceReuse> &inferenceReuse, std::unordered_set<const Section *> &graphStable,
		std::map<std::string, std::string> &fingerprints, InferenceSummaryStore &summaryStore, const Json *frozen,
		const InferenceSourceIndex &sourceIndex, InferenceGlobalInputs &globalInputs
	)
		: previous(previous), nativePrevious(nativePrevious), observations(observations), projection(projection), paths(paths),
		  reads(reads), frontendDiagnostics(frontendDiagnostics), statistics(statistics), irReuse(irReuse),
		  inferenceReuse(inferenceReuse), graphStable(graphStable), fingerprints(fingerprints), summaryStore(summaryStore),
		  frozen(frozen), sourceIndex(sourceIndex), globalInputs(globalInputs) {}

	void run(ParseContext &context, const std::function<bool(const ParseContext::IncrementalFunctionRequest &)> &infer) {
		auto start = Clock::now();
		const Json &prior = previous.at("all_function_projection");
		if (context.options.emitDebugInfo || context.options.traceExecution ||
			!compatibleObservations(previous, observations, context))
			return;
		InferenceUnitInputs lookupInputs(context, projection, paths, statistics, fingerprints);
		reads.finalizeInterfaces(previous.at("section_dependency_graph").at("callable_lookups"), [&](const auto &path) {
			return lookupInputs.declarationFingerprint(path);
		});
		InferenceValueIdentities identities(paths);
		auto eligible = std::make_shared<IncrementalUnitSelection>();
		auto replacements = std::make_shared<IncrementalUnitSelection>();
		auto nativeReplacements = std::make_shared<IncrementalUnitSelection>();
		SymbolicInferenceUnits symbolic = referencedInferenceFunctions(previous.at("inference_summaries"));
		std::map<InferenceUnitKey, Json> verifiedValues;
		std::set<InferenceUnitKey> unavailableValues;
		for (const auto &[path, variants] : previous.at("inference_summaries").items())
			for (const auto &[key, reference] : variants.items())
				if (!reference.at("failed").get<bool>() &&
					(reference.at("observed_values").get<bool>() || !reference.at("pure_values_complete").get<bool>()) &&
					!summaryStore.retain(reference))
					unavailableValues.emplace(path, key);
		InferenceUnitCheck check(
			context, projection, paths, previous.at("section_dependency_graph"), reads, *replacements, statistics, fingerprints,
			unavailableValues, verifiedValues
		);
		std::map<std::string, const Section *> sections;
		for (const auto &[section, path] : paths)
			sections.emplace(path, section);
		auto updateEligible = [&]() {
			if (nativePrevious)
				for (const auto &[name, owner] : nativePrevious->at("function_sections").items()) {
					const std::string path = owner.get<std::string>();
					const std::string key = nativePrevious->at("function_instantiations").at(name).get<std::string>();
					auto section = sections.find(path);
					if (section != sections.end() && reusableCodegenSection(*section->second) &&
						!replacements->contains(section->second, key) && check.unchanged({path, key}))
						eligible->insert(section->second, key);
				}
			for (const auto &[path, variants] : previous.at("inference_summaries").items()) {
				auto section = sections.find(path);
				if (section == sections.end())
					continue;
				for (const auto &[key, summary] : variants.items())
					if ((summary.at("failed").get<bool>() || !summary.at("body").is_null() ||
						 inferenceSummaryHasValueState(summary) || symbolic.contains({path, key})) &&
						!replacements->contains(section->second, key) && check.unchanged({path, key}))
						eligible->insert(section->second, key);
			}
		};
		updateEligible();
		auto nativeEligible = std::make_shared<IncrementalUnitSelection>();
		auto updateNativeEligible = [&]() {
			if (!nativePrevious)
				return;
			const auto &nativeUnits = nativePrevious->at("section_dependency_graph").at("inference_units");
			const auto &units = previous.at("section_dependency_graph").at("inference_units");
			for (const auto &[section, keys] : eligible->sections()) {
				const auto &path = paths.at(section);
				for (const auto &key : keys)
					if (nativeUnits.contains(path) && nativeUnits.at(path).contains(key) &&
						units.at(path).at(key) == nativeUnits.at(path).at(key))
						nativeEligible->insert(section, key);
			}
		};
		updateNativeEligible();
		if (nativePrevious)
			irReuse = std::make_unique<IncrementalIRReuse>(
				context, nativePrevious->at("generated_ir").get_ref<const std::string &>(),
				nativePrevious->at("function_sections"), nativePrevious->at("callable_functions"),
				nativePrevious->at("function_instantiations"), paths, nativeEligible, nativeReplacements,
				[queries = &globalInputs, nativeUnits = &nativePrevious->at("section_dependency_graph").at("inference_units"),
				 currentPaths = &paths](const Section *section, const std::string &key) {
				const auto &path = currentPaths->at(section);
				return nativeUnits->contains(path) && nativeUnits->at(path).contains(key) &&
					   queries->unchanged(nativeUnits->at(path).at(key).at("global_inputs"));
			}
			);
		inferenceReuse = std::make_unique<IncrementalInferenceReuse>(
			context, previous.at("inference_summaries"), previous.at("section_dependency_graph").at("inference_units"),
			globalInputs, irReuse.get(), paths, eligible, summaryStore, sourceIndex, frozen
		);
		context.reuseInferredInstantiation = [this](
												 Section *section, const InstantiationKey &key, Instantiation &instantiation,
												 ParseContext::InferenceReuseUse use, InferenceContext *caller
											 ) {
			return inferenceReuse->reuse(section, key, instantiation, use, caller);
		};
		context.restoreInferredBody = [this](Section *section, Instantiation &instance, InferenceContext *caller) {
			return inferenceReuse->loadBody(section, instance, caller);
		};
		std::set<InferenceUnitKey> native;
		if (nativePrevious)
			for (const auto &[name, owner] : nativePrevious->at("function_sections").items())
				if (!nativePrevious->at("callable_functions").contains(name))
					native.emplace(
						owner.get<std::string>(), nativePrevious->at("function_instantiations").at(name).get<std::string>()
					);
		std::set<const Section *> capturedSections;
		std::unique_ptr<InferenceBodySources> diagnosticSources;
		for (auto &[unit, request] : collectRequests(*eligible, native, symbolic)) {
			if (check.unchanged(unit)) {
				eligible->insert(request.section, unit.second);
				continue;
			}
			const Json *priorSummary = restoreArguments(unit, request);
			if (!priorSummary)
				continue;
			if (!infer(request))
				continue;
			statistics.preinferredInstantiations++;
			InferenceStorageIdentities storage(context, paths, identities, sourceIndex);
			Json current = captureSectionInferenceSummary(
				request.section, identities, storage, globalInputs, &statistics.preinferSummaryInstantiationsCaptured, nullptr,
				&unit.second
			);
			capturedSections.insert(request.section);
			const auto &instance = request.section->instantiations.at(request.key);
			auto diagnostics = captureInferenceDiagnostics(context, instance.storageScope, [&]() -> auto & {
				if (!diagnosticSources)
					diagnosticSources = std::make_unique<InferenceBodySources>(context, identities);
				return *diagnosticSources;
			}, storage);
			const Json &priorDiagnostics = previous.at("inference_summaries").at(unit.first).at(unit.second).at("diagnostics");
			if (diagnostics && *diagnostics == priorDiagnostics && current.contains(unit.second) &&
				sameInterface(current.at(unit.second), *priorSummary)) {
				replacements->insert(request.section, unit.second);
				if (nativePrevious) {
					const auto &nativeSummaries = nativePrevious->at("inference_summaries");
					if (nativeSummaries.contains(unit.first) && nativeSummaries.at(unit.first).contains(unit.second)) {
						const auto &reference = nativeSummaries.at(unit.first).at(unit.second);
						const Json *nativeSummary = summaryStore.load(reference);
						if (!reference.at("failed").get<bool>() && nativeSummary &&
							sameInterface(current.at(unit.second), *nativeSummary) &&
							*diagnostics == reference.at("diagnostics"))
							nativeReplacements->insert(request.section, unit.second);
					}
				}
				Json values = Json::object();
				for (const Json &observation : current.at(unit.second).at("pure_values")) {
					Json identity = inferenceValueFingerprints(observation.at(0), observation.at(1));
					values[identity.at(0).get<std::string>()] = std::move(identity.at(1));
				}
				verifiedValues[unit] = std::move(values);
				if (native.contains(unit))
					context.incrementalReplacementFunctions.push_back(request);
			}
		}
		statistics.preinferSummarySectionsCaptured += capturedSections.size();
		updateEligible();
		updateNativeEligible();
		const Json &oldVariants = previous.at("section_dependency_graph").at("instantiations");
		for (const auto &[section, keys] : eligible->sections()) {
			const auto &variants = oldVariants.at(paths.at(section));
			if (std::ranges::all_of(variants.items(), [&](const auto &entry) {
				return keys.contains(entry.key());
			})) {
				graphStable.insert(section);
				statistics.dependencyGraphSectionsReused++;
			}
		}
		bool rootEligible = nativePrevious == &previous && compatibleFunctionProjection(prior, projection) &&
							check.unchanged({"root", aggregateGraphVariant}) &&
							prior.at("declarations") == projection.at("declarations") &&
							prior.at("initial_matcher_tree") == projection.at("initial_matcher_tree") &&
							prior.at("final_matcher_tree") == projection.at("final_matcher_tree") &&
							sameRootGlobalBindings(prior.at("global_resolution"), projection.at("global_resolution")) &&
							prior.at("scope_rule_bodies") == projection.at("scope_rule_bodies") &&
							reusableRootExecution(context, previous, frontendDiagnostics, paths);
		if (rootEligible && reuseRootWithGlobalTypes(context, previous, paths, *irReuse)) {
			graphStable.insert(context.mainSection);
			restoreInferenceDiagnostics(context, {}, previous.at("root_diagnostics"), [&]() -> auto & {
				if (!diagnosticSources)
					diagnosticSources = std::make_unique<InferenceBodySources>(context, identities);
				return *diagnosticSources;
			});
			if (auto root = context.deferredInferenceDiagnostics.find({}); root != context.deferredInferenceDiagnostics.end())
				for (const Diagnostic &diagnostic : root->second)
					context.addDiagnostic(diagnostic);
		}
		statistics.preinferenceMilliseconds = elapsedMilliseconds(start);
	}

  private:
	using Request = ParseContext::IncrementalFunctionRequest;
	static bool sameInterface(const Json &current, const Json &prior) {
		return std::ranges::all_of(current.items(), [&](const auto &entry) {
			return entry.key() == "pure_values" || entry.key() == "pure_values_complete" ||
				   prior.at(entry.key()) == entry.value();
		});
	}
	const Json *restoreArguments(const InferenceUnitKey &unit, Request &request) {
		statistics.preinferArgumentVariantsDecoded++;
		const Json *saved = summaryStore.load(previous.at("inference_summaries").at(unit.first).at(unit.second));
		if (!saved)
			return nullptr;
		bool decoded = inferenceReuse->decodeValues(
			*saved, previous.at("inference_summaries").at(unit.first).at(unit.second),
			[&](const InferenceValueIdentities &identities) {
			const Json &summary = *saved;
			auto key = decodeInferenceKey(summary, identities);
			if (!key || key->argumentTypes.size() != request.parameterNames.size() ||
				inferenceSummaryKey(*key, identities) != unit.second)
				return false;
			request.key = std::move(*key);
			request.requiredCompileTimeParameters =
				summary.at("parameters").at("required_compile_time").get<std::unordered_set<std::string>>();
			for (const Json &observation : summary.at("pure_values")) {
				std::vector<CompileTimeValue> values;
				for (const Json &encoded : observation.at(0)) {
					auto value = decodeInferenceValue(encoded, identities);
					if (!value)
						return false;
					values.push_back(std::move(*value));
				}
				request.observedArguments.push_back(std::move(values));
			}
			return true;
		}
		);
		return decoded ? saved : nullptr;
	}

	static std::optional<std::vector<std::string>> parameterNames(const Section &section) {
		if (section.type == SectionType::Retain || section.type == SectionType::Release)
			return std::vector<std::string>{std::string(managedLifecycleParameterName)};
		if (section.type != SectionType::Function || section.isFlex || section.patternDefinitions.size() != 1 ||
			section.patternDefinitions.front()->indexedPaths.size() != 1)
			return std::nullopt;
		std::vector<std::string> names;
		for (const PatternElement &element : section.patternDefinitions.front()->indexedPaths.front())
			if (element.type == PatternElement::Variable) {
				if (element.isArgumentPack)
					return std::nullopt;
				names.push_back(element.text);
			}
		return names;
	}

	std::vector<std::pair<InferenceUnitKey, Request>> collectRequests(
		const IncrementalUnitSelection &eligible, const std::set<InferenceUnitKey> &native,
		const SymbolicInferenceUnits &symbolic
	) {
		const Json &prior = previous.at("all_function_projection"), &summaries = previous.at("inference_summaries");
		std::map<InferenceUnitKey, Request> candidates;
		for (const auto &[section, path] : paths) {
			if (!hasNoGlobalAccess(section) || !summaries.contains(path) || !prior.at("function_declarations").contains(path) ||
				!projection.at("function_declarations").contains(path) ||
				prior.at("function_declarations").at(path) != projection.at("function_declarations").at(path))
				continue;
			auto names = parameterNames(*section);
			if (!names)
				continue;
			statistics.preinferDependencyGraphSectionsChecked++;
			for (const auto &[key, summary] : summaries.at(path).items()) {
				if (summary.at("failed").get<bool>())
					continue;
				if ((!native.contains({path, key}) && !inferenceSummaryHasValueState(summary) &&
					 !symbolic.contains({path, key})) ||
					!summary.at("global_inputs").empty() || eligible.contains(section, key))
					continue;
				candidates.emplace(InferenceUnitKey{path, key}, Request{const_cast<Section *>(section), *names, {}, {}, {}});
			}
		}
		std::vector<std::pair<InferenceUnitKey, Request>> ordered;
		std::set<InferenceUnitKey> visited;
		const Json &units = previous.at("section_dependency_graph").at("inference_units");
		std::function<void(const InferenceUnitKey &)> visit = [&](const InferenceUnitKey &unit) {
			if (!visited.insert(unit).second)
				return;
			for (const Json &callee : units.at(unit.first).at(unit.second).at("callees"))
				visit({callee.at(0).get<std::string>(), callee.at(1).get<std::string>()});
			if (auto found = candidates.find(unit); found != candidates.end())
				ordered.push_back(*found);
		};
		for (const auto &[unit, _] : candidates)
			visit(unit);
		return ordered;
	}
	const Json &previous;
	const Json *nativePrevious;
	const Json &observations, &projection;
	const std::map<const Section *, std::string> &paths;
	IncrementalInferenceReads &reads;
	const Json &frontendDiagnostics;
	BuildStatistics &statistics;
	std::unique_ptr<IncrementalIRReuse> &irReuse;
	std::unique_ptr<IncrementalInferenceReuse> &inferenceReuse;
	std::unordered_set<const Section *> &graphStable;
	std::map<std::string, std::string> &fingerprints;
	InferenceSummaryStore &summaryStore;
	const Json *frozen;
	const InferenceSourceIndex &sourceIndex;
	InferenceGlobalInputs &globalInputs;
};
