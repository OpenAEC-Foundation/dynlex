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
	const std::string *onlyKey = nullptr, std::map<std::string, const Instantiation *> *capturedInstances = nullptr
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
		if (capturedInstances)
			(*capturedInstances)[identity] = &instantiation;
		if (capturedInstantiations)
			(*capturedInstantiations)++;
	}
	return summaries;
}

Json captureInferenceFactClasses(
	const Json &summary, const InferenceValueIdentities &identities, const std::function<InferenceBodySources &()> &sources
) {
	std::set<std::string> required;
	(void)remapInferenceSummaryTypes(summary, [&](const Json &type) {
		collectInferenceClassDependencies(type, required);
		return nullptr;
	});
	Json records = Json::object();
	std::stack<std::string> pending;
	for (const auto &path : required)
		pending.push(path);
	while (!pending.empty()) {
		std::string path = std::move(pending.top());
		pending.pop();
		if (records.contains(path))
			continue;
		auto *definition = static_cast<ClassSection *>(identities.sections.at(path))->classDefinition;
		const Json &state = sources().classInstances(definition);
		records[path] = state;
		for (const auto &dependency : classInstantiationDependencies(state))
			pending.push(dependency);
	}
	return records;
}

SymbolicInferenceUnits nativeInferenceUnits(const Json &functions, const Json &keys) {
	SymbolicInferenceUnits result;
	for (const auto &[name, owner] : functions.items())
		result.emplace(owner.get<std::string>(), keys.at(name).get<std::string>());
	return result;
}

Json captureInferenceSummaries(
	ParseContext &context, const std::map<const Section *, std::string> &sectionPaths,
	const std::set<std::string> &excludedSources, const Json &globalTypes, InferenceSummaryStore &summaryStore,
	const SymbolicInferenceUnits &native, const IncrementalUnitSelection *skipped = nullptr, const Json *prior = nullptr
) {
	Json summaries = Json::object();
	InferenceValueIdentities identities(sectionPaths);
	InferenceStorageIdentities storage(context, sectionPaths, identities);
	std::unique_ptr<InferenceBodySources> sources;
	auto sourceRecords = [&]() -> InferenceBodySources & {
		if (!sources)
			sources = std::make_unique<InferenceBodySources>(context, identities);
		return *sources;
	};
	for (const auto &[section, path] : sectionPaths) {
		if (inferenceSummarySourceExcluded(section, excludedSources))
			continue;
		std::map<std::string, const Instantiation *> instances;
		Json variants =
			captureSectionInferenceSummary(section, identities, storage, globalTypes, nullptr, skipped, nullptr, &instances);
		for (const auto &[key, summary] : variants.items()) {
			const Instantiation &instance = *instances.at(key);
			Json body = nullptr;
			bool needsBody =
				(instance.purity == InstantiationPurity::Pure && instance.returnType.kind != DataType::Kind::Void) ||
				!native.contains({path, key});
			if (needsBody && instance.inferenceReusedFromCache && prior && prior->contains(path) &&
				prior->at(path).contains(key))
				body = prior->at(path).at(key).at("body");
			else if (needsBody && instance.bodyInferred) {
				InferenceBodyWriter writer(identities, sourceRecords(), storage);
				body = summaryStore.saveBody(writer.capture(instance));
			}
			Json classes = summaryStore.saveClasses(captureInferenceFactClasses(summary, identities, sourceRecords));
			summaries[path][key] = summaryStore.save(summary, std::move(body), std::move(classes));
		}
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
				instantiation.needsReinfer |= !hasIR && !instantiation.bodyInferred;
				if (hasIR)
					runtimeValidated.insert(std::move(unit));
			}
			if (instantiation.needsReinfer)
				loadBody(section, key, instantiation, caller);
			return true;
		}
		{
			Restoration transaction(*this, caller);
			if (!transaction.prepare(section, key, instantiation, use) || !transaction.finish())
				return false;
		}
		if (instantiation.needsReinfer)
			loadBody(section, key, instantiation, caller);
		return true;
	}

	bool loadBody(Section *section, Instantiation &instance, InferenceContext *caller) {
		for (const auto &[key, slot] : section->instantiations)
			if (&slot == &instance)
				return loadBody(section, key, instance, caller);
		crashCompilerBug("inferred body request has no owning instantiation");
	}
	bool loadBody(Section *section, const InstantiationKey &key, Instantiation &instance, InferenceContext *caller) {
		if (instance.bodyInferred)
			return true;
		auto identity = inferenceSummaryKey(key, identities);
		if (!identity || !eligible->contains(section, *identity))
			return false;
		const auto &reference = summaries.at(sectionPaths.at(section)).at(*identity);
		auto start = Clock::now();
		auto record = summaryStore.loadBody(reference.at("body"));
		if (!record)
			return false;
		if (!bodySources)
			bodySources = std::make_unique<InferenceBodySources>(context, identities);
		Restoration transaction(*this, caller);
		std::set<std::string> classPaths;
		for (const auto &[path, _] : reference.at("body").at("classes").items())
			classPaths.insert(path);
		if (!transaction.classes(reference.at("body").at("classes"), classPaths))
			return false;
		InferenceBodyReader reader(
			context, identities, *bodySources, storage, [&](Section *owner, const InstantiationKey &arguments) {
			return transaction.instance(owner, arguments);
		}
		);
		std::shared_ptr<InstantiatedSectionBody> body;
		try {
			body = reader.restore(*record);
		} catch (const UnavailableInferenceBody &) {
			return false;
		}
		if (!transaction.finish())
			return false;
		reader.commit();
		recordInferenceRestoration(caller, section, key);
		instance.body = std::move(body);
		instance.bodyInferred = true;
		instance.needsReinfer = false;
		context.inferenceBodiesLoaded++;
		context.inferenceBodyLoadMilliseconds += elapsedMilliseconds(start);
		return true;
	}

	bool decodeValues(
		const Json &summary, const Json &reference, const std::function<bool(const InferenceValueIdentities &)> &decode
	) {
		Restoration transaction(*this, nullptr);
		return transaction.types(summary, reference) && decode(identities) && transaction.finish();
	}

	size_t reusedCount() const { return count; }
	size_t sourceExpressionLookups() const { return storage.sourceExpressionLookups(); }
	size_t sourceExpressionLinesIndexed() const { return storage.sourceExpressionLinesIndexed(); }
	Json snapshot(
		const std::map<const Section *, std::string> &paths, const std::set<std::string> &excludedSources,
		const Json &globalTypes, const Json &functions, const Json &functionKeys, size_t *reusedSections = nullptr
	) const {
		auto native = nativeInferenceUnits(functions, functionKeys);
		IncrementalUnitSelection skipped;
		if (globalTypes == priorGlobalTypes)
			for (const auto &[section, keys] : eligible->sections())
				for (const auto &[key, instantiation] : section->instantiations)
					if (instantiation.inferenceReusedFromCache && !instantiation.valueSummaryChanged)
						if (auto identity = inferenceSummaryKey(key, identities); identity && keys.contains(*identity))
							skipped.insert(section, *identity);
		Json result =
			captureInferenceSummaries(context, paths, excludedSources, globalTypes, summaryStore, native, &skipped, &summaries);
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
					if (!native.contains({path, key}) && summary.at("body").is_null() &&
						!inferenceSummaryHasValueState(summary))
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
		auto retireBody = [&](const std::string &path, const std::string &key, Json &reference) {
			if (native.contains({path, key}) && !reference.at("body").is_null() &&
				!reference.at("body").at("for_values").get<bool>())
				reference["body"] = nullptr;
		};
		for (auto &[path, variants] : result.items())
			for (auto &[key, reference] : variants.items())
				retireBody(path, key, reference);
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
				retireBody(path, key, result[path][key]);
				if (!freshSections.contains(path))
					copiedSections.insert(path);
			}
			for (const Json &target : result.at(path).at(key).at("symbolic_dependencies"))
				pending.emplace(target.at(0).get<std::string>(), target.at(1).get<std::string>());
			if (!result.at(path).at(key).at("body").is_null())
				for (const Json &target : result.at(path).at(key).at("body").at("dependencies"))
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
	std::unique_ptr<InferenceBodySources> bodySources;
	Restoration *restoration = nullptr;
	std::set<std::pair<const Section *, std::string>> runtimeValidated;
	size_t count = 0;
};
