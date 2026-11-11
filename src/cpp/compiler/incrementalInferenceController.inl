// Inference replay and IR import share concrete-unit eligibility. A rebuilt
// interface stops propagation only for the instantiation that was compared.
class IncrementalInferenceController {
  public:
	IncrementalInferenceController(
		const Json &previous, const Json &observations, const Json &projection,
		const std::map<const Section *, std::string> &paths, const IncrementalInferenceReads &reads,
		const Json &frontendDiagnostics, BuildStatistics &statistics, std::unique_ptr<IncrementalIRReuse> &irReuse,
		std::unique_ptr<IncrementalInferenceReuse> &inferenceReuse, std::unordered_set<const Section *> &graphStable,
		std::map<std::string, std::string> &fingerprints
	)
		: previous(previous), observations(observations), projection(projection), paths(paths), reads(reads),
		  frontendDiagnostics(frontendDiagnostics), statistics(statistics), irReuse(irReuse), inferenceReuse(inferenceReuse),
		  graphStable(graphStable), fingerprints(fingerprints) {}

	void run(ParseContext &context, const std::function<bool(const ParseContext::IncrementalFunctionRequest &)> &infer) {
		auto start = Clock::now();
		const Json &prior = previous.at("all_function_projection");
		if (context.options.emitDebugInfo || context.options.traceExecution ||
			!compatibleObservations(previous, observations, context) ||
			(!compatibleFunctionProjection(prior, projection) && !compatibleGlobalFreeProjection(prior, projection)))
			return;
		ClassIdentityPaths classes = classIdentityPaths(paths);
		auto eligible = std::make_shared<IncrementalUnitSelection>();
		auto replacements = std::make_shared<IncrementalUnitSelection>();
		InferenceUnitCheck check(
			context, projection, paths, previous.at("section_dependency_graph"), reads, *replacements, statistics, fingerprints
		);
		std::map<std::string, const Section *> sections;
		for (const auto &[section, path] : paths)
			sections.emplace(path, section);
		auto updateEligible = [&]() {
			for (const auto &[name, owner] : previous.at("function_sections").items()) {
				const std::string path = owner.get<std::string>();
				const std::string key = previous.at("function_instantiations").at(name).get<std::string>();
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
					if (inferenceSummaryHasObservedValues(summary) && !replacements->contains(section->second, key) &&
						check.unchanged({path, key}))
						eligible->insert(section->second, key);
			}
		};
		updateEligible();
		irReuse = std::make_unique<IncrementalIRReuse>(
			context, previous.at("generated_ir").get_ref<const std::string &>(), previous.at("function_sections"),
			previous.at("callable_functions"), previous.at("function_instantiations"), paths, eligible, replacements
		);
		inferenceReuse = std::make_unique<IncrementalInferenceReuse>(
			context, previous.at("inference_summaries"), previous.at("global_types"), *irReuse, paths, eligible
		);
		context.reuseInferredInstantiation = [this](
												 Section *section, const InstantiationKey &key, Instantiation &instantiation,
												 ParseContext::InferenceReuseUse use
											 ) {
			return inferenceReuse->reuse(section, key, instantiation, use);
		};
		std::set<InferenceUnitKey> native;
		for (const auto &[name, owner] : previous.at("function_sections").items())
			if (!previous.at("callable_functions").contains(name))
				native.emplace(owner.get<std::string>(), previous.at("function_instantiations").at(name).get<std::string>());
		std::set<const Section *> capturedSections;
		for (auto &[unit, request] : collectRequests(*eligible, native)) {
			if (check.unchanged(unit)) {
				eligible->insert(request.section, unit.second);
				continue;
			}
			if (!restoreArguments(unit, request, classes))
				continue;
			if (!infer(request))
				continue;
			statistics.preinferredInstantiations++;
			InferenceStorageIdentities storage(context, paths, classes);
			Json current = captureSectionInferenceSummary(
				request.section, classes, storage, Json(nullptr), &statistics.preinferSummaryInstantiationsCaptured, nullptr,
				&unit.second
			);
			capturedSections.insert(request.section);
			const Json &priorSummary = previous.at("inference_summaries").at(unit.first).at(unit.second);
			if (current.contains(unit.second) && priorSummary.at("pure_values_complete").get<bool>() &&
				current.at(unit.second) == priorSummary) {
				replacements->insert(request.section, unit.second);
				if (native.contains(unit))
					context.incrementalReplacementFunctions.push_back(request);
			}
		}
		statistics.preinferSummarySectionsCaptured += capturedSections.size();
		updateEligible();
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
		bool rootEligible = compatibleFunctionProjection(prior, projection) &&
							check.unchanged({"root", aggregateGraphVariant}) &&
							prior.at("declarations") == projection.at("declarations") &&
							prior.at("initial_matcher_tree") == projection.at("initial_matcher_tree") &&
							prior.at("final_matcher_tree") == projection.at("final_matcher_tree") &&
							sameRootGlobalBindings(prior.at("global_resolution"), projection.at("global_resolution")) &&
							prior.at("scope_rule_bodies") == projection.at("scope_rule_bodies") &&
							reusableRootExecution(context, previous, frontendDiagnostics, paths);
		if (rootEligible && reuseRootWithGlobalTypes(context, previous, paths, *irReuse))
			graphStable.insert(context.mainSection);
		statistics.preinferenceMilliseconds = elapsedMilliseconds(start);
	}

  private:
	using Request = ParseContext::IncrementalFunctionRequest;
	bool restoreArguments(const InferenceUnitKey &unit, Request &request, const ClassIdentityPaths &classes) {
		statistics.preinferArgumentVariantsDecoded++;
		const Json &summary = previous.at("inference_summaries").at(unit.first).at(unit.second);
		auto key = decodeInferenceKey(summary, classes);
		if (!key || key->argumentTypes.size() != request.parameterNames.size() ||
			inferenceSummaryKey(*key, classes) != unit.second)
			return false;
		request.key = std::move(*key);
		request.requiredCompileTimeParameters =
			summary.at("parameters").at("required_compile_time").get<std::unordered_set<std::string>>();
		if (summary.at("pure_values_complete").get<bool>()) {
			for (const Json &observation : summary.at("pure_values")) {
				std::vector<CompileTimeValue> values;
				for (const Json &encoded : observation.at(0)) {
					auto value = decodeInferenceValue(encoded, classes);
					if (!value)
						return false;
					values.push_back(std::move(*value));
				}
				request.observedArguments.push_back(std::move(values));
			}
		}
		return true;
	}
	std::vector<std::pair<InferenceUnitKey, Request>>
	collectRequests(const IncrementalUnitSelection &eligible, const std::set<InferenceUnitKey> &native) {
		const Json &prior = previous.at("all_function_projection"), &summaries = previous.at("inference_summaries");
		std::map<InferenceUnitKey, Request> candidates;
		for (const auto &[section, path] : paths) {
			if (section->type != SectionType::Function || section->isFlex || !hasNoGlobalAccess(section) ||
				!summaries.contains(path) || section->patternDefinitions.size() != 1 ||
				section->patternDefinitions.front()->indexedPaths.size() != 1 ||
				!prior.at("function_declarations").contains(path) || !projection.at("function_declarations").contains(path) ||
				prior.at("function_declarations").at(path) != projection.at("function_declarations").at(path))
				continue;
			statistics.preinferDependencyGraphSectionsChecked++;
			std::vector<std::string> names;
			bool pack = false;
			for (const PatternElement &element : section->patternDefinitions.front()->indexedPaths.front())
				if (element.type == PatternElement::Variable) {
					pack |= element.isArgumentPack;
					names.push_back(element.text);
				}
			if (pack)
				continue;
			for (const auto &[key, summary] : summaries.at(path).items()) {
				if ((!native.contains({path, key}) && !inferenceSummaryHasObservedValues(summary)) ||
					summary.at("needs_global_types").get<bool>() || eligible.contains(section, key))
					continue;
				candidates.emplace(InferenceUnitKey{path, key}, Request{const_cast<Section *>(section), names, {}, {}, {}});
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
	const Json &previous, &observations, &projection;
	const std::map<const Section *, std::string> &paths;
	const IncrementalInferenceReads &reads;
	const Json &frontendDiagnostics;
	BuildStatistics &statistics;
	std::unique_ptr<IncrementalIRReuse> &irReuse;
	std::unique_ptr<IncrementalInferenceReuse> &inferenceReuse;
	std::unordered_set<const Section *> &graphStable;
	std::map<std::string, std::string> &fingerprints;
};
