// Cached inference summaries are applied only when every matched dependency
// has an unchanged body. Other instantiations follow normal type inference.
bool hasNoGlobalAccess(const Section *section) {
	if (!section->globalVariables.empty())
		return false;
	for (const Section *child : section->children) {
		if (child->runsInEnclosingBody() && !hasNoGlobalAccess(child))
			return false;
	}
	return true;
}

Json encodePureReturnValues(const Section *section, const Instantiation &instantiation, const ClassIdentityPaths &classes) {
	Json results = Json::array();
	if (instantiation.purity != InstantiationPurity::Pure || !hasNoGlobalAccess(section))
		return results;
	for (const auto &[arguments, value] : instantiation.pureReturnValuesByArguments) {
		auto encodedValue = encodeInferenceValue(value, classes);
		if (!encodedValue)
			continue;
		Json encodedArguments = Json::array();
		for (const CompileTimeValue &argument : arguments) {
			auto encoded = encodeInferenceValue(argument, classes);
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
std::optional<PureReturnValues> decodePureReturnValues(const Json &encoded, const ClassIdentityPaths &classes) {
	PureReturnValues results;
	for (const Json &entry : encoded) {
		std::vector<CompileTimeValue> arguments;
		for (const Json &argument : entry.at(0)) {
			auto value = decodeInferenceValue(argument, classes);
			if (!value)
				return std::nullopt;
			arguments.push_back(std::move(*value));
		}
		auto value = decodeInferenceValue(entry.at(1), classes);
		if (!value)
			return std::nullopt;
		results.emplace(std::move(arguments), std::move(*value));
	}
	return results;
}

bool canPersistInferenceSummary(
	const Section *section, const InstantiationKey &key, const Instantiation &instantiation,
	const ClassIdentityPaths &classPaths
) {
	std::set<std::pair<const ClassDefinition *, int>> visiting;
	return section->type == SectionType::Function && !section->isFlex && instantiation.valid && !instantiation.inferring &&
		   !instantiation.needsReinfer && instantiation.body &&
		   inferenceTypeIdentity(instantiation.returnType, classPaths, visiting).has_value() &&
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
	const Section *section, const ClassIdentityPaths &classPaths, InferenceStorageIdentities &storage, const Json &globalTypes,
	size_t *capturedInstantiations = nullptr, const IncrementalUnitSelection *skipped = nullptr,
	const std::string *onlyKey = nullptr
) {
	Json summaries = Json::object();
	for (const auto &[key, instantiation] : section->instantiations) {
		if (!canPersistInferenceSummary(section, key, instantiation, classPaths))
			continue;
		auto encodedKey = encodeInferenceKey(key, classPaths);
		if (!encodedKey)
			continue;
		const std::string identity = digest(encodedKey->dump());
		if ((skipped && skipped->contains(section, identity)) || (onlyKey && identity != *onlyKey))
			continue;
		auto parameters = encodeInferenceParameters(instantiation, classPaths);
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
		Json pureValues = encodePureReturnValues(section, instantiation, classPaths);
		bool pureValuesComplete =
			instantiation.pureReturnValuesComplete && pureValues.size() == instantiation.pureReturnValuesByArguments.size();
		Json record = std::move(*encodedKey);
		record.update(
			Json{
				{"parameters", std::move(*parameters)},
				{"return_type", *inferenceTypeIdentity(instantiation.returnType, classPaths, visiting)},
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
	ClassIdentityPaths classPaths = classIdentityPaths(sectionPaths);
	InferenceStorageIdentities storage(context, sectionPaths, classPaths);
	for (const auto &[section, path] : sectionPaths) {
		if (inferenceSummarySourceExcluded(section, excludedSources))
			continue;
		Json variants = captureSectionInferenceSummary(section, classPaths, storage, globalTypes, nullptr, skipped);
		for (const auto &[key, summary] : variants.items())
			summaries[path][key] = summaryStore.save(summary);
	}
	return summaries;
}

class IncrementalInferenceReuse {
  public:
	IncrementalInferenceReuse(
		ParseContext &context, const Json &summaries, const Json &priorGlobalTypes, const IncrementalIRReuse &irReuse,
		const std::map<const Section *, std::string> &sectionPaths, std::shared_ptr<IncrementalUnitSelection> eligible,
		InferenceSummaryStore &summaryStore
	)
		: context(context), summaries(summaries), priorGlobalTypes(priorGlobalTypes), irReuse(irReuse),
		  eligible(std::move(eligible)), sectionPaths(sectionPaths), classPaths(classIdentityPaths(sectionPaths)),
		  storage(context, sectionPaths, classPaths), summaryStore(summaryStore) {}

	bool
	reuse(Section *section, const InstantiationKey &key, Instantiation &instantiation, ParseContext::InferenceReuseUse use) {
		std::optional<std::string> keyText = inferenceSummaryKey(key, classPaths);
		if (!keyText || !eligible->contains(section, *keyText))
			return false;
		const std::string &path = sectionPaths.at(section);
		if (!summaries.contains(path) || !summaries.at(path).contains(*keyText))
			return false;
		const Json *saved = summaryStore.load(summaries.at(path).at(*keyText));
		if (!saved)
			return false;
		const Json &summary = *saved;
		auto unit = std::pair{static_cast<const Section *>(section), *keyText};
		if (instantiation.inferenceReusedFromCache) {
			if (use == ParseContext::InferenceReuseUse::RuntimeCall && !runtimeValidated.contains(unit)) {
				bool hasIR =
					irReuse.hasReusableIRForInstantiation(section, *keyText, summary.at("needs_global_types").get<bool>());
				instantiation.needsReinfer |= !hasIR;
				if (hasIR)
					runtimeValidated.insert(std::move(unit));
			}
			return true;
		}
		auto returnType = decodeInferenceTypeIdentity(summary.at("return_type"), classPaths);
		if (!returnType)
			return false;
		bool needsGlobalTypes = summary.at("needs_global_types").get<bool>();
		if (needsGlobalTypes) {
			auto currentGlobalTypes = captureGlobalTypeProjection(sectionPaths);
			if (!currentGlobalTypes || *currentGlobalTypes != priorGlobalTypes)
				return false;
		}
		bool pureValue = summary.at("purity").get<int>() == static_cast<int>(InstantiationPurity::Pure) &&
						 returnType->kind != DataType::Kind::Void;
		bool mayUseObservedValues = pureValue && !needsGlobalTypes;
		bool hasIR = false;
		if (!mayUseObservedValues || use == ParseContext::InferenceReuseUse::RuntimeCall)
			hasIR = irReuse.hasReusableIRForInstantiation(section, *keyText, needsGlobalTypes);
		if (!hasIR && !mayUseObservedValues)
			return false;
		auto parameters = decodeInferenceParameters(summary.at("parameters"), classPaths);
		auto pureValues = decodePureReturnValues(summary.at("pure_values"), classPaths);
		if (!parameters || !pureValues)
			return false;
		if (!storage.canDecodeProvenance(summary.at("return_provenance")) ||
			!canDecodeGlobalEffects(summary.at("global_effects"), storage))
			return false;
		instantiation.returnType = std::move(*returnType);
		applyInferenceParameters(std::move(*parameters), instantiation);
		instantiation.purity = static_cast<InstantiationPurity>(summary.at("purity").get<int>());
		instantiation.hasReturnAddressProvenance = summary.at("has_return_address_provenance").get<bool>();
		instantiation.returnAddressProvenance = storage.decodeProvenance(summary.at("return_provenance"));
		instantiation.returnPointerStorageParameterName =
			summary.at("return_storage_parameter").is_null()
				? std::nullopt
				: std::optional<std::string>{summary.at("return_storage_parameter").get<std::string>()};
		instantiation.returnPointerStorageAmbiguous = summary.at("return_storage_ambiguous").get<bool>();
		instantiation.fallsThrough = summary.at("falls_through").get<bool>();
		instantiation.hasReturnIntrinsic = summary.at("return_intrinsic").get<bool>();
		instantiation.writesThroughUnknownAddress = summary.at("unknown_write").get<bool>();
		instantiation.externallyEscapesUnknownAddress = summary.at("unknown_escape").get<bool>();
		decodeGlobalEffects(section, summary.at("global_effects"), instantiation, storage);
		instantiation.pureReturnValuesByArguments = std::move(*pureValues);
		instantiation.pureReturnValuesComplete = summary.at("pure_values_complete").get<bool>();
		instantiation.inferenceReusedFromCache = true;
		instantiation.needsReinfer = (use == ParseContext::InferenceReuseUse::RuntimeCall && !hasIR) ||
									 (use == ParseContext::InferenceReuseUse::Callable && pureValue);
		if (hasIR)
			runtimeValidated.insert(std::move(unit));
		count++;
		return true;
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
						if (auto identity = inferenceSummaryKey(key, classPaths); identity && keys.contains(*identity))
							skipped.insert(section, *identity);
		Json result = captureInferenceSummaries(context, paths, excludedSources, globalTypes, summaryStore, &skipped);
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
				if (copied && !encodedCurrentVariants && reusedSections)
					(*reusedSections)++;
			}
		}
		return result;
	}

  private:
	ParseContext &context;
	const Json &summaries;
	const Json &priorGlobalTypes;
	const IncrementalIRReuse &irReuse;
	std::shared_ptr<IncrementalUnitSelection> eligible;
	const std::map<const Section *, std::string> &sectionPaths;
	ClassIdentityPaths classPaths;
	InferenceStorageIdentities storage;
	InferenceSummaryStore &summaryStore;
	std::set<std::pair<const Section *, std::string>> runtimeValidated;
	size_t count = 0;
};
