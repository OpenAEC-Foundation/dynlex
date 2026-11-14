private:
InferenceUnitCheck &queryCheck() {
	if (!dynamicCheck)
		dynamicCheck = std::make_unique<InferenceUnitCheck>(
			*inferredContext, projection, paths, previous.at("section_dependency_graph"), reads, *semanticReplacements,
			statistics, fingerprints, unavailableValues, verifiedValues
		);
	return *dynamicCheck;
}
InferenceUnitCheck &nativeQueryCheck() {
	if (!nativeCheck)
		nativeCheck = std::make_unique<InferenceUnitCheck>(
			*inferredContext, projection, paths, nativePrevious->at("section_dependency_graph"), reads,
			*nativeInterfaceReplacements, statistics, fingerprints, unavailableValues, verifiedValues
		);
	return *nativeCheck;
}
void updateEligibility(const InferenceUnitKey &unit) {
	Section *section = inferenceReuse->section(unit);
	bool stable = queryCheck().unchanged(unit);
	if (stable)
		semanticEligible->insert(section, unit.second);
	else
		semanticEligible->erase(section, unit.second);
	if (!nativePrevious)
		return;
	if (stable && nativeQueryCheck().unchanged(unit))
		nativeEligibleUnits->insert(section, unit.second);
	else
		nativeEligibleUnits->erase(section, unit.second);
}
bool equivalent(const Json &current, const Json &prior) {
	const Json *fresh = summaryStore.load(current), *saved = summaryStore.load(prior);
	return fresh && saved && current.at("diagnostics") == prior.at("diagnostics") && sameInterface(*fresh, *saved);
}
void adoptQueries() {
	std::set<InferenceUnitKey> accepted;
	const auto &updates = products.updates();
	for (; publicationsObserved < updates.size(); publicationsObserved++) {
		const auto &key = updates[publicationsObserved];
		const Json &reference = products.references().at(key.first).at(key.second);
		if (inferenceReuse->accept(key, reference, products.inputProof(key), products.variants().at(key.first).at(key.second)))
			accepted.insert(key);
	}
	if (accepted.empty())
		return;
	for (const auto &key : accepted) {
		Section *section = inferenceReuse->section(key);
		const Json &current = *inferenceReuse->reference(key);
		const Json &summaries = previous.at("inference_summaries");
		bool same = summaries.contains(key.first) && summaries.at(key.first).contains(key.second) &&
					equivalent(current, summaries.at(key.first).at(key.second));
		if (same)
			semanticReplacements->insert(section, key.second);
		else
			semanticReplacements->erase(section, key.second);
		bool nativeSame = false;
		if (nativePrevious) {
			const Json &nativeSummaries = nativePrevious->at("inference_summaries");
			nativeSame = nativeSummaries.contains(key.first) && nativeSummaries.at(key.first).contains(key.second) &&
						 equivalent(current, nativeSummaries.at(key.first).at(key.second));
		}
		inferenceReuse->nativeInterface(key, nativeSame);
		if (nativeSame)
			nativeInterfaceReplacements->insert(section, key.second);
		else
			nativeInterfaceReplacements->erase(section, key.second);
		Json values = Json::object();
		const Json *summary = summaryStore.load(current);
		if (!current.at("failed").get<bool>())
			for (const Json &observation : summary->at("pure_values")) {
				Json identity = inferenceValueFingerprints(observation.at(0), observation.at(1));
				values[identity.at(0).get<std::string>()] = std::move(identity.at(1));
			}
		verifiedValues[key] = std::move(values);
	}
	queryCheck().interfacesChanged();
	if (nativePrevious) {
		nativeQueryCheck().interfacesChanged();
		for (const auto &key : accepted)
			if (nativeInterfaceReplacements->contains(inferenceReuse->section(key), key.second) &&
				nativeQueryCheck().unchanged(key) &&
				inferenceReuse->nativeInputsUnchanged(inferenceReuse->section(key), key.second))
				nativeInterfaceReplacements->erase(inferenceReuse->section(key), key.second);
		nativeQueryCheck().interfacesChanged();
	}
	for (const auto &key : accepted)
		updateEligibility(key);
	pendingNativeReplacements.insert(accepted.begin(), accepted.end());
	for (const auto &key : accepted)
		if (nativeBodies.contains(key) && nativeInterfaceReplacements->contains(inferenceReuse->section(key), key.second))
			summaryStore.retainReplacementBody(inferenceReuse->reference(key)->at("body"));
}

public:
void activateReplacements() {
	adoptQueries();
	while (!pendingNativeReplacements.empty()) {
		auto node = pendingNativeReplacements.extract(pendingNativeReplacements.begin());
		const auto &key = node.value();
		if (!nativeBodies.contains(key) || !nativeInterfaceReplacements->contains(inferenceReuse->section(key), key.second))
			continue;
		auto prepared = preparedRequests.extract(key);
		Request replacement{inferenceReuse->section(key), {}, {}, {}, {}};
		if (!prepared.empty())
			replacement = std::move(prepared.mapped());
		else
			requireCompilerInvariant(restoreArguments(key, replacement), "native replacement lost its concrete arguments");
		auto &slot = replacement.section->instantiations[replacement.key];
		requireCompilerInvariant(
			inferenceReuse->reuse(
				replacement.section, replacement.key, slot, ParseContext::InferenceReuseUse::RuntimeCall, nullptr
			) && slot.valid &&
				slot.bodyInferred,
			"native replacement lost its completed inferred body"
		);
		if (scheduledReplacements.insert(key).second)
			inferredContext->incrementalReplacementFunctions.push_back(std::move(replacement));
	}
	summaryStore.releaseReplacementBodies();
}

private:
bool probe(const InferenceUnitKey &unit, ParseContext::IncrementalFunctionRequest &request) {
	auto started = Clock::now();
	InferenceSummaryStore::BodyTransferWindow bodyTransfers(summaryStore);
	probing = true;
	bool succeeded = probeFunction(request);
	probing = false;
	if (preparing)
		statistics.preinferredInstantiations += succeeded;
	else
		statistics.deferredQueryProbes++;
	adoptQueries();
	if (!preparing)
		statistics.deferredQueryMilliseconds += elapsedMilliseconds(started);
	return inferenceReuse->refreshed(unit.first, unit.second) && inferenceReuse->inputsUnchanged(unit);
}
bool refreshUnit(const InferenceUnitKey &unit, bool allowProbe = true) {
	if (!inferenceReuse->reference(unit))
		return inferenceReuse->inputsUnchanged(unit) && queryCheck().unchanged(unit);
	if (inferenceReuse->inputsUnchanged(unit) &&
		(inferenceReuse->refreshed(unit.first, unit.second) || queryCheck().unchanged(unit))) {
		updateEligibility(unit);
		return true;
	}
	if (!refreshing.insert(unit).second)
		return false;
	const Json &units = previous.at("section_dependency_graph").at("inference_units");
	bool oldBody = units.contains(unit.first) && units.at(unit.first).contains(unit.second) &&
				   !inferenceReuse->refreshed(unit.first, unit.second) && queryCheck().directInputsUnchanged(unit);
	Json proof = oldBody ? units.at(unit.first).at(unit.second).at("direct_global_inputs") : Json(nullptr);
	oldBody = oldBody && globalInputs.unchanged(proof);
	if (oldBody) {
		const Json &record = units.at(unit.first).at(unit.second);
		for (const Json &edge : record.at("callees")) {
			InferenceUnitKey child{edge.at(0).get<std::string>(), edge.at(1).get<std::string>()};
			if (!refreshUnit(child, allowProbe) ||
				(inferenceReuse->refreshed(child.first, child.second) &&
				 !semanticReplacements->contains(inferenceReuse->section(child), child.second))) {
				oldBody = false;
				break;
			}
			mergeInferenceGlobalInputs(proof, *inferenceReuse->inputProof(child));
		}
		if (oldBody)
			for (const Json &query : record.at("values")) {
				InferenceUnitKey child{query.at(0).get<std::string>(), query.at(1).get<std::string>()};
				if (!refreshUnit(child, allowProbe) || !inferenceReuse->valueUnchanged(child, query)) {
					oldBody = false;
					break;
				}
				mergeInferenceGlobalInputs(proof, *inferenceReuse->inputProof(child));
			}
	}
	bool available = oldBody && !proof.is_null();
	if (available) {
		inferenceReuse->validateInputs(unit, std::move(proof));
		semanticReplacements->insert(inferenceReuse->section(unit), unit.second);
		if (!preparing)
			statistics.deferredQueryInterfacesPreserved++;
		updateEligibility(unit);
	} else if (allowProbe && units.contains(unit.first) && units.at(unit.first).contains(unit.second)) {
		Request request{inferenceReuse->section(unit), {}, {}, {}, {}};
		available = restoreArguments(unit, request) && probe(unit, request);
	}
	refreshing.erase(unit);
	return available;
}
IncrementalInferenceProducts & products;
ParseContext *inferredContext = nullptr;
std::function<bool(const ParseContext::IncrementalFunctionRequest &)> probeFunction;
std::shared_ptr<IncrementalUnitSelection> semanticEligible, semanticReplacements, nativeEligibleUnits,
	nativeInterfaceReplacements;
std::map<InferenceUnitKey, Json> verifiedValues;
std::map<InferenceUnitKey, Request> preparedRequests;
std::set<InferenceUnitKey> unavailableValues, nativeBodies, scheduledReplacements, pendingNativeReplacements, refreshing;
bool probing = false, preparing = false;
size_t publicationsObserved = 0;
std::unique_ptr<InferenceUnitCheck> dynamicCheck, nativeCheck;
