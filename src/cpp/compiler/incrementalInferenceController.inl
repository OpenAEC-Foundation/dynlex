// Inference replay and IR import share concrete-unit eligibility. A rebuilt
// interface stops propagation only for the instantiation that was compared.
class IncrementalInferenceController {
	using Request = ParseContext::IncrementalFunctionRequest;

  public:
	IncrementalInferenceController(
		const Json &previous, const Json *nativePrevious, const Json &observations, const Json &projection,
		const std::map<const Section *, std::string> &paths, IncrementalInferenceReads &reads, const Json &frontendDiagnostics,
		BuildStatistics &statistics, std::unique_ptr<IncrementalIRReuse> &irReuse,
		std::unique_ptr<IncrementalInferenceReuse> &inferenceReuse, std::unordered_set<const Section *> &graphStable,
		std::map<std::string, std::string> &fingerprints, InferenceSummaryStore &summaryStore, const Json *frozen,
		const InferenceSourceIndex &sourceIndex, InferenceGlobalInputs &globalInputs, IncrementalInferenceProducts &products
	)
		: products(products), previous(previous), nativePrevious(nativePrevious), observations(observations),
		  projection(projection), paths(paths), reads(reads), frontendDiagnostics(frontendDiagnostics), statistics(statistics),
		  irReuse(irReuse), inferenceReuse(inferenceReuse), graphStable(graphStable), fingerprints(fingerprints),
		  summaryStore(summaryStore), frozen(frozen), sourceIndex(sourceIndex), globalInputs(globalInputs) {}

	void run(ParseContext &context, const std::function<bool(const ParseContext::IncrementalFunctionRequest &)> &infer) {
		auto start = Clock::now();
		const Json &prior = previous.at("all_function_projection");
		if (context.options.emitDebugInfo || context.options.traceExecution ||
			!compatibleObservations(previous, observations, context))
			return;
		inferredContext = &context;
		preparing = true;
		probeFunction = infer;
		InferenceUnitInputs lookupInputs(context, projection, paths, statistics, fingerprints);
		reads.finalizeInterfaces(previous.at("section_dependency_graph").at("callable_lookups"), [&](const auto &path) {
			return lookupInputs.declarationFingerprint(path);
		});
		InferenceValueIdentities identities(paths);
		auto eligible = std::make_shared<IncrementalUnitSelection>();
		auto replacements = std::make_shared<IncrementalUnitSelection>();
		auto nativeReplacements = std::make_shared<IncrementalUnitSelection>();
		semanticEligible = eligible;
		semanticReplacements = replacements;
		nativeInterfaceReplacements = nativeReplacements;
		SymbolicInferenceUnits symbolic = referencedInferenceFunctions(previous.at("inference_summaries"));
		for (const auto &[path, variants] : previous.at("inference_summaries").items())
			for (const auto &[key, reference] : variants.items())
				if (!reference.at("failed").get<bool>() &&
					(reference.at("observed_values").get<bool>() || !reference.at("pure_values_complete").get<bool>()) &&
					!summaryStore.retain(reference))
					unavailableValues.emplace(path, key);
		auto &check = queryCheck();
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
		nativeEligibleUnits = nativeEligible;
		auto updateNativeEligible = [&]() {
			if (!nativePrevious)
				return;
			for (const auto &[section, keys] : eligible->sections()) {
				const auto &path = paths.at(section);
				for (const auto &key : keys)
					if (nativeQueryCheck().unchanged({path, key}))
						nativeEligible->insert(section, key);
			}
		};
		updateNativeEligible();
		if (nativePrevious)
			irReuse = std::make_unique<IncrementalIRReuse>(
				context, nativePrevious->at("generated_ir").get_ref<const std::string &>(),
				nativePrevious->at("function_sections"), nativePrevious->at("callable_functions"),
				nativePrevious->at("function_instantiations"), paths, nativeEligible, nativeReplacements,
				[queries = &inferenceReuse](const Section *section, const std::string &key) {
				return (*queries)->nativeInputsUnchanged(section, key);
			}
			);
		inferenceReuse = std::make_unique<IncrementalInferenceReuse>(
			context, previous.at("inference_summaries"), previous.at("section_dependency_graph").at("inference_units"),
			previous.at("section_dependency_graph").at("instantiations"), globalInputs, irReuse.get(), paths, eligible,
			summaryStore, sourceIndex, frozen, nativePrevious
		);
		inferenceReuse->retainRestored = [products = &products](
											 const auto &unit, const auto &scope, const auto &reference, const auto &proof,
											 const Json *prior, const auto &identity, const auto &selected
										 ) {
			products->retain(unit, scope, reference, proof, prior, identity, selected);
		};
		context.reuseInferredInstantiation = [this](
												 Section *section, const InstantiationKey &key, Instantiation &instantiation,
												 ParseContext::InferenceReuseUse use, InferenceContext *caller
											 ) {
			adoptQueries();
			auto unit = inferenceReuse->identify(section, key);
			if (!unit || !refreshUnit(*unit, !probing))
				return false;
			return inferenceReuse->reuse(section, key, instantiation, use, caller);
		};
		context.activateIncrementalReplacements = [this]() {
			activateReplacements();
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
		nativeBodies = native;
		bool rootInputsUnchanged = nativePrevious == &previous && compatibleFunctionProjection(prior, projection) &&
								   prior.at("declarations") == projection.at("declarations") &&
								   prior.at("initial_matcher_tree") == projection.at("initial_matcher_tree") &&
								   prior.at("final_matcher_tree") == projection.at("final_matcher_tree") &&
								   sameRootGlobalBindings(prior.at("global_resolution"), projection.at("global_resolution")) &&
								   prior.at("scope_rule_bodies") == projection.at("scope_rule_bodies");
		std::set<const Section *> capturedSections;
		std::unique_ptr<InferenceBodySources> diagnosticSources;
		// Only entry replay needs speculative requests from the previous executable.
		// A changed entry discovers its current typed calls through ordinary demand.
		if (rootInputsUnchanged)
			for (auto &[unit, request] : collectRequests(*eligible, native, symbolic)) {
				if (check.unchanged(unit)) {
					eligible->insert(request.section, unit.second);
					continue;
				}
				if (restoreArguments(unit, request) && probe(unit, request)) {
					capturedSections.insert(request.section);
					statistics.preinferSummaryInstantiationsCaptured++;
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
		bool rootEligible = rootInputsUnchanged && check.unchanged({"root", aggregateGraphVariant}) &&
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
		preparing = false;
	}

#include "incrementalInferenceDeferredQueries.inl"

  private:
	static bool sameInterface(const Json &current, const Json &prior) {
		if (current.at("failure").is_null() != prior.at("failure").is_null())
			return false;
		if (current.at("invocation").at(3) != prior.at("invocation").at(3))
			return false;
		return std::ranges::all_of(current.items(), [&](const auto &entry) {
			return entry.key() == "pure_values" || entry.key() == "pure_values_complete" || entry.key() == "invocation" ||
				   entry.key() == "global_inputs" || entry.key() == "pure_value_inputs" ||
				   (prior.contains(entry.key()) && prior.at(entry.key()) == entry.value());
		});
	}
	const Json *restoreArguments(const InferenceUnitKey &unit, Request &request) {
		statistics.preinferArgumentVariantsDecoded++;
		const Json *reference = inferenceReuse->reference(unit);
		if (!reference)
			return nullptr;
		const Json *saved = summaryStore.load(*reference);
		if (!saved)
			return nullptr;
		bool decoded = inferenceReuse->decodeValues(*saved, *reference, [&](const InferenceValueIdentities &identities) {
			const Json &summary = *saved;
			auto invocation = decodeInferenceInvocation(*request.section, summary.at("invocation"));
			if (invocation.declaration != globalInputs.declaration(*request.section))
				return false;
			request.definition = invocation.definition;
			request.parameterNames = std::move(invocation.names);
			request.requiredCompileTimeParameters = std::move(invocation.requiredCompileTime);
			auto key = decodeInferenceKey(summary, identities);
			if (!key || key->argumentTypes.size() != request.parameterNames.size() ||
				inferenceSummaryKey(*key, identities) != unit.second)
				return false;
			request.key = std::move(*key);
			if (!summary.at("failure").is_null())
				return true;
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
		});
		// These identities are materialized before entry trials start, so their
		// class layouts and callable slots survive every later trial rollback.
		if (decoded && preparing)
			preparedRequests.insert_or_assign(unit, request);
		return decoded ? saved : nullptr;
	}

	std::vector<std::pair<InferenceUnitKey, Request>> collectRequests(
		const IncrementalUnitSelection &eligible, const std::set<InferenceUnitKey> &native,
		const SymbolicInferenceUnits &symbolic
	) {
		const Json &prior = previous.at("all_function_projection"), &summaries = previous.at("inference_summaries");
		std::map<InferenceUnitKey, Request> candidates;
		for (const auto &[section, path] : paths) {
			if (!hasNoGlobalAccess(section) || !summaries.contains(path) || !prior.at("matches").contains(path) ||
				!projection.at("matches").contains(path) ||
				prior.at("declaration_fingerprints").at(path) != projection.at("declaration_fingerprints").at(path))
				continue;
			statistics.preinferDependencyGraphSectionsChecked++;
			for (const auto &[key, summary] : summaries.at(path).items()) {
				if (summary.at("failed").get<bool>())
					continue;
				if ((!native.contains({path, key}) && !inferenceSummaryHasValueState(summary) &&
					 !symbolic.contains({path, key})) ||
					!previous.at("section_dependency_graph")
						 .at("inference_units")
						 .at(path)
						 .at(key)
						 .at("global_inputs")
						 .empty() ||
					eligible.contains(section, key))
					continue;
				candidates.emplace(InferenceUnitKey{path, key}, Request{const_cast<Section *>(section), {}, {}, {}, {}});
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
