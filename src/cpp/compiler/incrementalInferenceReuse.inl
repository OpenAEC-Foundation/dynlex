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
	bool callable = (section->type == SectionType::Function && !section->isFlex) || section->type == SectionType::Retain ||
					section->type == SectionType::Release;
	if (!callable || instantiation.inferring || instantiation.needsReinfer)
		return false;
	if (instantiation.failure) {
		for (const auto &name : instantiation.requiredCompileTimeParameters)
			if (std::ranges::none_of(key.compileTimeParameters, [&](const auto &entry) {
				return entry.first == name && isCompileTimeKnown(entry.second);
			}))
				return false;
		return true;
	}
	if (!instantiation.valid || !instantiation.returnType.isDeduced())
		return false;
	requireCompilerInvariant(instantiation.storageScope != nullptr, "inferred unit has no storage scope");
	return inferenceTypeIdentity(instantiation.returnType, identities.classes, visiting).has_value() &&
		   instantiation.argumentTypes == key.argumentTypes &&
		   (!instantiation.returnType.isMetaType() || instantiation.purity == InstantiationPurity::Pure) &&
		   !instantiation.memoryViolation;
}

bool inferenceSummaryHasValueState(const Json &summary) {
	return !summary.at("failed").get<bool>() &&
		   summary.at("purity").get<int>() == static_cast<int>(InstantiationPurity::Pure) &&
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
		if (instantiation.failure) {
			bool needsGlobals = !hasNoGlobalAccess(section) || !instantiation.accessedGlobalReferences.empty();
			if (needsGlobals && globalTypes.is_null())
				continue;
			Json record = std::move(*encodedKey);
			record["needs_global_types"] = needsGlobals;
			summaries[identity] = std::move(record);
			if (capturedInstances)
				(*capturedInstances)[identity] = &instantiation;
			continue;
		}
		auto parameters = encodeInferenceParameters(instantiation, identities);
		if (!parameters)
			continue;
		auto globalEffects = encodeGlobalEffects(section, instantiation, storage);
		if (!globalEffects)
			continue;
		auto returnProvenance = storage.encodeProvenance(instantiation.returnAddressProvenance);
		if (!returnProvenance)
			continue;
		bool needsGlobalTypes = !hasNoGlobalAccess(section) || !instantiation.accessedGlobalReferences.empty() ||
								!instantiation.writtenGlobalReferences.empty() ||
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
				{"failure", nullptr},
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
	const Json &summary, const InferenceValueIdentities &identities, InferenceClassSnapshot &classSnapshot
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
		const Json &state = classSnapshot.instances(definition);
		records[path] = state;
		for (const auto &dependency : classInstantiationDependencies(state))
			pending.push(dependency);
	}
	return records;
}

// Observations belong to the same immutable pure query even when successive
// rejected trials restore its earlier in-memory argument table.
void mergeInferenceObservedValues(Json &current, const Json &prior) {
	if (!current.at("failure").is_null() || !prior.at("failure").is_null() ||
		current.at("purity") != static_cast<int>(InstantiationPurity::Pure) || current.at("purity") != prior.at("purity") ||
		current.at("needs_global_types").get<bool>() || prior.at("needs_global_types").get<bool>() ||
		current.at("return_type") != prior.at("return_type"))
		return;
	std::map<Json, Json> observations;
	for (const Json *record : std::array<const Json *, 2>{&prior, &current})
		for (const Json &entry : record->at("pure_values")) {
			auto [found, inserted] = observations.emplace(entry.at(0), entry.at(1));
			requireCompilerInvariant(inserted || found->second == entry.at(1), "pure query produced inconsistent observations");
		}
	current["pure_values"] = Json::array();
	for (auto &[arguments, value] : observations)
		current["pure_values"].push_back(Json::array({arguments, value}));
	current["pure_values_complete"] =
		current.at("pure_values_complete").get<bool>() && prior.at("pure_values_complete").get<bool>();
}

SymbolicInferenceUnits nativeInferenceUnits(const Json &functions, const Json &keys) {
	SymbolicInferenceUnits result;
	for (const auto &[name, owner] : functions.items())
		result.emplace(owner.get<std::string>(), keys.at(name).get<std::string>());
	return result;
}

Json captureInferenceSummaries(
	ParseContext &context, const InferenceSourceIndex &sourceIndex, const std::map<const Section *, std::string> &sectionPaths,
	const Json &globalTypes, InferenceSummaryStore &summaryStore, const SymbolicInferenceUnits &native,
	const IncrementalUnitSelection *skipped = nullptr, const Json *prior = nullptr, bool keepBodies = false,
	const Json *frozen = nullptr
) {
	Json summaries = frozen ? *frozen : Json::object();
	InferenceValueIdentities identities(sectionPaths);
	InferenceStorageIdentities storage(context, sectionPaths, identities, sourceIndex);
	InferenceClassSnapshot classSnapshot(identities);
	std::unique_ptr<InferenceBodySources> sources;
	auto sourceRecords = [&]() -> InferenceBodySources & {
		if (!sources)
			sources = std::make_unique<InferenceBodySources>(context, identities);
		return *sources;
	};
	for (const auto &[section, path] : sectionPaths) {
		std::map<std::string, const Instantiation *> instances;
		Json variants =
			captureSectionInferenceSummary(section, identities, storage, globalTypes, nullptr, skipped, nullptr, &instances);
		for (auto &[key, summary] : variants.items()) {
			const Instantiation &instance = *instances.at(key);
			if (instance.failure)
				summary["failure"] = captureInferenceFailure(*instance.failure, sourceRecords());
			if (summaries.contains(path) && summaries.at(path).contains(key)) {
				const Json *retained = summaryStore.load(summaries.at(path).at(key));
				requireCompilerInvariant(retained, "newly captured inference observations disappeared");
				mergeInferenceObservedValues(summary, *retained);
			}
			auto diagnostics = captureInferenceDiagnostics(context, instance.storageScope, sourceRecords, storage);
			if (!diagnostics)
				continue;
			Json body = nullptr;
			bool needsBody =
				!instance.failure &&
				((instance.purity == InstantiationPurity::Pure && instance.returnType.kind != DataType::Kind::Void) ||
				 keepBodies || !native.contains({path, key}));
			if (needsBody && instance.inferenceReusedFromCache && prior && prior->contains(path) &&
				prior->at(path).contains(key))
				body = prior->at(path).at(key).at("body");
			else if (needsBody && instance.bodyInferred) {
				InferenceBodyWriter writer(identities, sourceRecords(), storage, classSnapshot);
				body = summaryStore.saveBody(writer.capture(instance));
			}
			Json classes = summaryStore.saveClasses(captureInferenceFactClasses(summary, identities, classSnapshot));
			summaries[path][key] = summaryStore.save(summary, std::move(body), std::move(classes), std::move(*diagnostics));
		}
	}
	return summaries;
}

class IncrementalInferenceReuse {
#include "incrementalInferenceRestoration.inl"
  public:
	IncrementalInferenceReuse(
		ParseContext &context, const Json &summaries, const Json &priorGlobalTypes, const IncrementalIRReuse *irReuse,
		const std::map<const Section *, std::string> &sectionPaths, std::shared_ptr<IncrementalUnitSelection> eligible,
		InferenceSummaryStore &summaryStore, const InferenceSourceIndex &sourceIndex, const Json *frozen = nullptr
	)
		: context(context), summaries(summaries), priorGlobalTypes(priorGlobalTypes), irReuse(irReuse),
		  eligible(std::move(eligible)), sectionPaths(sectionPaths), identities(sectionPaths),
		  storage(context, sectionPaths, identities, sourceIndex), sourceIndex(sourceIndex), summaryStore(summaryStore),
		  frozen(frozen) {
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
			if (instantiation.failure)
				return true;
			auto unit = std::pair{static_cast<const Section *>(section), *keyText};
			if ((use == ParseContext::InferenceReuseUse::RuntimeCall || use == ParseContext::InferenceReuseUse::Callable) &&
				!runtimeValidated.contains(unit)) {
				bool hasIR = irReuse && irReuse->hasReusableIRForInstantiation(section, *keyText, [&]() {
					return globalTypesUnchanged();
				});
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
		requireCompilerInvariant(body->storageScope == instance.storageScope, "restored body changed its unit scope");
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
	Json snapshot(
		const std::map<const Section *, std::string> &paths, const Json &globalTypes, const Json &functions,
		const Json &functionKeys, size_t *reusedSections = nullptr, bool keepBodies = false
	) const {
		auto native = nativeInferenceUnits(functions, functionKeys);
		IncrementalUnitSelection skipped;
		if (globalTypes == priorGlobalTypes)
			for (const auto &[section, keys] : eligible->sections())
				for (const auto &[key, instantiation] : section->instantiations)
					if (instantiation.inferenceReusedFromCache && !instantiation.valueSummaryChanged)
						if (auto identity = inferenceSummaryKey(key, identities); identity && keys.contains(*identity))
							skipped.insert(section, *identity);
		Json result = captureInferenceSummaries(
			context, sourceIndex, paths, globalTypes, summaryStore, native, &skipped, &summaries, keepBodies, frozen
		);
		std::set<std::string> freshSections, copiedSections;
		for (const auto &[path, _] : result.items())
			freshSections.insert(path);
		for (const auto &[section, keys] : eligible->sections()) {
			const std::string &path = sectionPaths.at(section);
			if (summaries.contains(path)) {
				bool encodedCurrentVariants = result.contains(path) && !result.at(path).empty();
				bool copied = false;
				for (const auto &[key, summary] : summaries.at(path).items()) {
					if (!keys.contains(key))
						continue;
					if (!summary.at("failed").get<bool>() && !native.contains({path, key}) && summary.at("body").is_null() &&
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
		for (auto &[path, variants] : result.items())
			for (auto &[key, reference] : variants.items())
				if (!reference.at("failed").get<bool>() && reference.at("body").is_null() &&
					eligible->contains(sections.at(path), key) && summaries.contains(path) &&
					summaries.at(path).contains(key) &&
					(!reference.at("needs_global_types").get<bool>() || globalTypes == priorGlobalTypes))
					reference["body"] = summaries.at(path).at(key).at("body");
		auto retireBody = [&](const std::string &path, const std::string &key, Json &reference) {
			if (!keepBodies && native.contains({path, key}) && !reference.at("body").is_null() &&
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
				if (section == sections.end() || !eligible->contains(section->second, key) || !summaries.contains(path) ||
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
	bool globalTypesUnchanged() const {
		auto current = captureGlobalTypeProjection(sectionPaths);
		return current && *current == priorGlobalTypes;
	}
	ParseContext &context;
	const Json &summaries;
	const Json &priorGlobalTypes;
	const IncrementalIRReuse *irReuse;
	std::shared_ptr<IncrementalUnitSelection> eligible;
	const std::map<const Section *, std::string> &sectionPaths;
	InferenceValueIdentities identities;
	InferenceStorageIdentities storage;
	const InferenceSourceIndex &sourceIndex;
	InferenceSummaryStore &summaryStore;
	const Json *frozen;
	std::unique_ptr<InferenceBodySources> bodySources;
	Restoration *restoration = nullptr;
	std::set<std::pair<const Section *, std::string>> runtimeValidated;
	size_t count = 0;
};
