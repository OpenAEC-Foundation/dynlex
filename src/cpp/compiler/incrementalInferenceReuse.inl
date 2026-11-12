// Reuse requires unchanged source inputs and validated dependency results for
// this concrete instantiation. Other instantiations follow ordinary inference.
bool hasNoGlobalAccess(const Section *section) {
	if (!section->globalVariables.empty())
		return false;
	for (const Section *child : section->children) {
		if (child->runsInEnclosingBody() && !hasNoGlobalAccess(child))
			return false;
	}
	return true;
}

Json encodePureReturnValues(
	const Section *section, const Instantiation &instantiation, const InferenceValueIdentities &identities
) {
	Json results = Json::array();
	if (instantiation.purity != InstantiationPurity::Pure || !hasNoGlobalAccess(section))
		return results;
	for (const auto &[arguments, value] : instantiation.pureReturnValuesByArguments) {
		auto encodedValue = encodeInferenceValue(value, identities);
		if (!encodedValue)
			continue;
		Json encodedArguments = Json::array();
		for (const CompileTimeValue &argument : arguments) {
			auto encoded = encodeInferenceValue(argument, identities);
			if (!encoded)
				break;
			encodedArguments.push_back(std::move(*encoded));
		}
		if (encodedArguments.size() == arguments.size())
			results.push_back(Json::array({std::move(encodedArguments), std::move(*encodedValue)}));
	}
	return results;
}

using PureReturnValues = std::map<std::vector<CompileTimeValue>, CompileTimeValue>;
std::optional<PureReturnValues> decodePureReturnValues(const Json &encoded, const InferenceValueIdentities &identities) {
	PureReturnValues results;
	for (const Json &entry : encoded) {
		std::vector<CompileTimeValue> arguments;
		for (const Json &argument : entry.at(0)) {
			auto value = decodeInferenceValue(argument, identities);
			if (!value)
				return std::nullopt;
			arguments.push_back(std::move(*value));
		}
		auto value = decodeInferenceValue(entry.at(1), identities);
		if (!value)
			return std::nullopt;
		results.emplace(std::move(arguments), std::move(*value));
	}
	return results;
}

bool canPersistInferenceSummary(
	const Section *section, const InstantiationKey &key, const Instantiation &instantiation,
	const InferenceValueIdentities &identities
) {
	std::set<std::pair<const ClassDefinition *, int>> visiting;
	return section->type == SectionType::Function && !section->isFlex && instantiation.valid && !instantiation.inferring &&
		   !instantiation.needsReinfer && instantiation.body &&
		   inferenceTypeIdentity(instantiation.returnType, identities.classes, visiting).has_value() &&
		   instantiation.argumentTypes == key.argumentTypes &&
		   (!instantiation.returnType.isMetaType() || instantiation.purity == InstantiationPurity::Pure) &&
		   !instantiation.memoryViolation;
}

bool inferenceSummarySourceExcluded(const Section *section, const std::set<std::string> &excludedSources) {
	return !section->openingLine || excludedSources.contains(section->openingLine->sourceFile->uri);
}

bool inferenceSummaryHasValueState(const Json &summary) {
	return summary.at("purity").get<int>() == static_cast<int>(InstantiationPurity::Pure) &&
		   !summary.at("needs_global_types").get<bool>() &&
		   (summary.at("observed_values").get<bool>() || !summary.at("pure_values_complete").get<bool>());
}

Json captureSectionInferenceSummary(
	const Section *section, const InferenceValueIdentities &identities, InferenceStorageIdentities &storage,
	const Json &globalTypes, size_t *capturedInstantiations = nullptr, const IncrementalUnitSelection *skipped = nullptr,
	const std::string *onlyKey = nullptr
) {
	Json summaries = Json::object();
	for (const auto &[key, instantiation] : section->instantiations) {
		if (!canPersistInferenceSummary(section, key, instantiation, identities))
			continue;
		auto encodedKey = encodeInferenceKey(key, identities);
		if (!encodedKey)
			continue;
		const std::string identity = digest(encodedKey->dump());
		if ((skipped && skipped->contains(section, identity)) || (onlyKey && identity != *onlyKey))
			continue;
		auto parameters = encodeInferenceParameters(instantiation, identities);
		if (!parameters)
			continue;
		auto globalEffects = encodeGlobalEffects(section, instantiation, storage);
		if (!globalEffects)
			continue;
		auto returnProvenance = storage.encodeProvenance(instantiation.returnAddressProvenance);
		if (!returnProvenance)
			continue;
		bool needsGlobalTypes = !hasNoGlobalAccess(section) || !instantiation.writtenGlobalReferences.empty() ||
								!instantiation.addressTakenGlobalReferences.empty() ||
								instantiation.externallyEscapedGlobalProvenance != AddressProvenance{};
		if (needsGlobalTypes && globalTypes.is_null())
			continue;
		std::set<std::pair<const ClassDefinition *, int>> visiting;
		Json pureValues = encodePureReturnValues(section, instantiation, identities);
		bool pureValuesComplete =
			instantiation.pureReturnValuesComplete && pureValues.size() == instantiation.pureReturnValuesByArguments.size();
		Json record = std::move(*encodedKey);
		record.update(
			Json{
				{"parameters", std::move(*parameters)},
				{"return_type", *inferenceTypeIdentity(instantiation.returnType, identities.classes, visiting)},
				{"purity", static_cast<int>(instantiation.purity)},
				{"has_return_address_provenance", instantiation.hasReturnAddressProvenance},
				{"return_provenance", std::move(*returnProvenance)},
				{"return_storage_parameter", instantiation.returnPointerStorageParameterName
												 ? Json(*instantiation.returnPointerStorageParameterName)
												 : Json(nullptr)},
				{"return_storage_ambiguous", instantiation.returnPointerStorageAmbiguous},
				{"falls_through", instantiation.fallsThrough},
				{"return_intrinsic", instantiation.hasReturnIntrinsic},
				{"unknown_write", instantiation.writesThroughUnknownAddress},
				{"unknown_escape", instantiation.externallyEscapesUnknownAddress},
				{"needs_global_types", needsGlobalTypes},
				{"pure_values", std::move(pureValues)},
				{"pure_values_complete", pureValuesComplete},
				{"global_effects", std::move(*globalEffects)}
			}
		);
		summaries[identity] = std::move(record);
		if (capturedInstantiations)
			(*capturedInstantiations)++;
	}
	return summaries;
}

Json captureInferenceSummaries(
	ParseContext &context, const std::map<const Section *, std::string> &sectionPaths,
	const std::set<std::string> &excludedSources, const Json &globalTypes, InferenceSummaryStore &summaryStore,
	const IncrementalUnitSelection *skipped = nullptr
) {
	Json summaries = Json::object();
	InferenceValueIdentities identities(sectionPaths);
	InferenceStorageIdentities storage(context, sectionPaths, identities);
	for (const auto &[section, path] : sectionPaths) {
		if (inferenceSummarySourceExcluded(section, excludedSources))
			continue;
		Json variants = captureSectionInferenceSummary(section, identities, storage, globalTypes, nullptr, skipped);
		for (const auto &[key, summary] : variants.items())
			summaries[path][key] = summaryStore.save(summary);
	}
	return summaries;
}

class IncrementalInferenceReuse {
#include "incrementalInferenceRestoration.inl"
  public:
	IncrementalInferenceReuse(
		ParseContext &context, const Json &summaries, const Json &priorGlobalTypes, const IncrementalIRReuse &irReuse,
		const std::map<const Section *, std::string> &sectionPaths, std::shared_ptr<IncrementalUnitSelection> eligible,
		InferenceSummaryStore &summaryStore
	)
		: context(context), summaries(summaries), priorGlobalTypes(priorGlobalTypes), irReuse(irReuse),
		  eligible(std::move(eligible)), sectionPaths(sectionPaths), identities(sectionPaths),
		  storage(context, sectionPaths, identities), summaryStore(summaryStore) {
		identities.restoreFunction = [this](const Json &encoded) {
			requireCompilerInvariant(restoration, "function value decoded outside a restoration transaction");
			return restoration->function(encoded);
		};
		identities.stagedFunction = [this](const CompileTimeFunction &function) -> std::optional<Json> {
			return restoration ? restoration->identity(function) : std::nullopt;
		};
	}

	bool reuse(
		Section *section, const InstantiationKey &key, Instantiation &instantiation, ParseContext::InferenceReuseUse use,
		InferenceContext *caller
	) {
		auto keyText = inferenceSummaryKey(key, identities);
		if (!keyText || !eligible->contains(section, *keyText))
			return false;
		if (instantiation.inferenceReusedFromCache) {
			auto unit = std::pair{static_cast<const Section *>(section), *keyText};
			if ((use == ParseContext::InferenceReuseUse::RuntimeCall || use == ParseContext::InferenceReuseUse::Callable) &&
				!runtimeValidated.contains(unit)) {
				const Json &reference = summaries.at(sectionPaths.at(section)).at(*keyText);
				bool hasIR =
					irReuse.hasReusableIRForInstantiation(section, *keyText, reference.at("needs_global_types").get<bool>());
				instantiation.needsReinfer |= !hasIR;
				if (hasIR)
					runtimeValidated.insert(std::move(unit));
			}
			return true;
		}
		Restoration transaction(*this, caller);
		return transaction.prepare(section, key, instantiation, use) && transaction.finish();
	}

	bool decodeValues(const std::function<bool(const InferenceValueIdentities &)> &decode) {
		Restoration transaction(*this, nullptr);
		return decode(identities) && transaction.finish();
	}

	size_t reusedCount() const { return count; }
	Json snapshot(
		const std::map<const Section *, std::string> &paths, const std::set<std::string> &excludedSources,
		const Json &globalTypes, const Json &functions, const Json &functionKeys, size_t *reusedSections = nullptr
	) const {
		std::set<std::pair<std::string, std::string>> native;
		for (const auto &[name, owner] : functions.items())
			native.emplace(owner.get<std::string>(), functionKeys.at(name).get<std::string>());
		IncrementalUnitSelection skipped;
		if (globalTypes == priorGlobalTypes)
			for (const auto &[section, keys] : eligible->sections())
				for (const auto &[key, instantiation] : section->instantiations)
					if (instantiation.inferenceReusedFromCache)
						if (auto identity = inferenceSummaryKey(key, identities); identity && keys.contains(*identity))
							skipped.insert(section, *identity);
		Json result = captureInferenceSummaries(context, paths, excludedSources, globalTypes, summaryStore, &skipped);
		std::set<std::string> freshSections, copiedSections;
		for (const auto &[path, _] : result.items())
			freshSections.insert(path);
		for (const auto &[section, keys] : eligible->sections()) {
			if (inferenceSummarySourceExcluded(section, excludedSources))
				continue;
			const std::string &path = sectionPaths.at(section);
			if (summaries.contains(path)) {
				bool encodedCurrentVariants = result.contains(path) && !result.at(path).empty();
				bool copied = false;
				for (const auto &[key, summary] : summaries.at(path).items()) {
					if (!keys.contains(key))
						continue;
					if (!native.contains({path, key}) && !inferenceSummaryHasValueState(summary))
						continue;
					if (summary.at("needs_global_types").get<bool>() && globalTypes != priorGlobalTypes)
						continue;
					if ((!result.contains(path) || !result.at(path).contains(key)) && summaryStore.retain(summary)) {
						result[path][key] = summary;
						copied = true;
					}
				}
				if (copied && !encodedCurrentVariants)
					copiedSections.insert(path);
			}
		}
		std::map<std::string, const Section *> sections;
		for (const auto &[section, path] : paths)
			sections.emplace(path, section);
		std::stack<std::pair<std::string, std::string>> pending;
		for (const auto &target : referencedInferenceFunctions(result))
			pending.push(target);
		SymbolicInferenceUnits visited;
		while (!pending.empty()) {
			auto [path, key] = pending.top();
			pending.pop();
			if (!visited.emplace(path, key).second)
				continue;
			if (!result.contains(path) || !result.at(path).contains(key)) {
				auto section = sections.find(path);
				// A reference describes a dependency, not a guarantee that its
				// independently evictable facts remain available in the cache.
				if (section == sections.end() || !eligible->contains(section->second, key) ||
					inferenceSummarySourceExcluded(section->second, excludedSources) || !summaries.contains(path) ||
					!summaries.at(path).contains(key))
					continue;
				const Json &reference = summaries.at(path).at(key);
				if (!summaryStore.retain(reference) ||
					(reference.at("needs_global_types").get<bool>() && globalTypes != priorGlobalTypes))
					continue;
				result[path][key] = reference;
				if (!freshSections.contains(path))
					copiedSections.insert(path);
			}
			for (const Json &target : result.at(path).at(key).at("symbolic_dependencies"))
				pending.emplace(target.at(0).get<std::string>(), target.at(1).get<std::string>());
		}
		if (reusedSections)
			*reusedSections += copiedSections.size();
		return result;
	}

  private:
	ParseContext &context;
	const Json &summaries;
	const Json &priorGlobalTypes;
	const IncrementalIRReuse &irReuse;
	std::shared_ptr<IncrementalUnitSelection> eligible;
	const std::map<const Section *, std::string> &sectionPaths;
	InferenceValueIdentities identities;
	InferenceStorageIdentities storage;
	InferenceSummaryStore &summaryStore;
	Restoration *restoration = nullptr;
	std::set<std::pair<const Section *, std::string>> runtimeValidated;
	size_t count = 0;
};
