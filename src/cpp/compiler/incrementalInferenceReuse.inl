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

bool isPortablePureValue(const CompileTimeValue &value) {
	return std::holds_alternative<WideInteger>(value) || std::holds_alternative<std::int64_t>(value) ||
		   std::holds_alternative<std::uint64_t>(value) || std::holds_alternative<double>(value) ||
		   std::holds_alternative<std::string>(value) || std::holds_alternative<bool>(value);
}

Json encodePureReturnValues(const Section *section, const Instantiation &instantiation) {
	Json results = Json::array();
	if (instantiation.purity != InstantiationPurity::Pure || !hasNoGlobalAccess(section))
		return results;
	for (const auto &[arguments, value] : instantiation.pureReturnValuesByArguments) {
		if (!isPortablePureValue(value) || !std::ranges::all_of(arguments, [](const CompileTimeValue &argument) {
			return isPortablePureValue(argument);
		}))
			continue;
		Json encodedArguments = Json::array();
		for (const CompileTimeValue &argument : arguments)
			encodedArguments.push_back(*encodeGlobalConstant(argument));
		results.push_back(Json::array({std::move(encodedArguments), *encodeGlobalConstant(value)}));
	}
	return results;
}

void decodePureReturnValues(const Json &encoded, Instantiation &instantiation) {
	for (const Json &entry : encoded) {
		std::vector<CompileTimeValue> arguments;
		for (const Json &argument : entry.at(0))
			arguments.push_back(decodeGlobalConstant(argument));
		instantiation.pureReturnValuesByArguments.emplace(std::move(arguments), decodeGlobalConstant(entry.at(1)));
	}
}

std::optional<std::string> inferenceSummaryKey(const InstantiationKey &key, const ClassIdentityPaths &classPaths) {
	if (!key.compileTimeParameters.empty())
		return std::nullopt;
	Json arguments = Json::array();
	for (const DataType &type : key.argumentTypes) {
		std::set<std::pair<const ClassDefinition *, int>> visiting;
		auto identity = inferenceTypeIdentity(type, classPaths, visiting);
		if (!identity)
			return std::nullopt;
		arguments.push_back(std::move(*identity));
	}
	return digest(arguments.dump());
}

bool canPersistInferenceSummary(
	const Section *section, const InstantiationKey &key, const Instantiation &instantiation,
	const ClassIdentityPaths &classPaths
) {
	std::set<std::pair<const ClassDefinition *, int>> visiting;
	return section->type == SectionType::Function && !section->isFlex && inferenceSummaryKey(key, classPaths).has_value() &&
		   instantiation.valid && !instantiation.inferring && !instantiation.needsReinfer && instantiation.body &&
		   inferenceTypeIdentity(instantiation.returnType, classPaths, visiting).has_value() &&
		   instantiation.argumentTypes == key.argumentTypes && instantiation.parameterSeedTypesByName.empty() &&
		   std::ranges::all_of(
			   instantiation.parameterOutputTypesByName,
			   [&](const auto &entry) {
		auto input = instantiation.parameterTypesByName.find(entry.first);
		return input != instantiation.parameterTypesByName.end() && input->second == entry.second;
	}
		   ) &&
		   instantiation.constantParameterValues.empty() && instantiation.requiredCompileTimeParameters.empty() &&
		   !instantiation.memoryViolation;
}

bool inferenceSummarySourceExcluded(const Section *section, const std::set<std::string> &excludedSources) {
	return !section->openingLine || excludedSources.contains(section->openingLine->sourceFile->uri);
}

Json captureSectionInferenceSummary(
	const Section *section, const ClassIdentityPaths &classPaths, InferenceStorageIdentities &storage, const Json &globalTypes,
	size_t *capturedInstantiations = nullptr
) {
	Json summaries = Json::object();
	for (const auto &[key, instantiation] : section->instantiations) {
		if (!canPersistInferenceSummary(section, key, instantiation, classPaths))
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
		Json argumentTypes = Json::array();
		for (const DataType &type : key.argumentTypes) {
			std::set<std::pair<const ClassDefinition *, int>> visiting;
			argumentTypes.push_back(*inferenceTypeIdentity(type, classPaths, visiting));
		}
		std::set<std::pair<const ClassDefinition *, int>> visiting;
		Json pureValues = encodePureReturnValues(section, instantiation);
		bool pureValuesComplete = pureValues.size() == instantiation.pureReturnValuesByArguments.size();
		summaries[*inferenceSummaryKey(key, classPaths)] = Json{
			{"argument_types", std::move(argumentTypes)},
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
		};
		if (capturedInstantiations)
			(*capturedInstantiations)++;
	}
	return summaries;
}

Json captureInferenceSummaries(
	ParseContext &context, const std::map<const Section *, std::string> &sectionPaths,
	const std::set<std::string> &excludedSources, const Json &globalTypes,
	const std::unordered_set<const Section *> *skipped = nullptr
) {
	Json summaries = Json::object();
	ClassIdentityPaths classPaths = classIdentityPaths(sectionPaths);
	InferenceStorageIdentities storage(context, sectionPaths, classPaths);
	for (const auto &[section, path] : sectionPaths) {
		if (inferenceSummarySourceExcluded(section, excludedSources) || (skipped && skipped->contains(section)))
			continue;
		Json variants = captureSectionInferenceSummary(section, classPaths, storage, globalTypes);
		if (!variants.empty())
			summaries[path] = std::move(variants);
	}
	return summaries;
}

class IncrementalInferenceReuse {
  public:
	IncrementalInferenceReuse(
		ParseContext &context, const Json &summaries, const Json &priorGlobalTypes, const IncrementalIRReuse &irReuse,
		const std::map<const Section *, std::string> &sectionPaths, std::unordered_set<const Section *> eligible
	)
		: context(context), summaries(summaries), priorGlobalTypes(priorGlobalTypes), irReuse(irReuse),
		  eligible(std::move(eligible)), sectionPaths(sectionPaths), classPaths(classIdentityPaths(sectionPaths)),
		  storage(context, sectionPaths, classPaths) {}

	bool reuse(Section *section, const InstantiationKey &key, Instantiation &instantiation, bool allowPureValueSummary) {
		std::optional<std::string> keyText = inferenceSummaryKey(key, classPaths);
		if (!eligible.contains(section) || !keyText)
			return false;
		const std::string &path = sectionPaths.at(section);
		if (!summaries.contains(path) || !summaries.at(path).contains(*keyText))
			return false;
		const Json &summary = summaries.at(path).at(*keyText);
		auto returnType = decodeInferenceTypeIdentity(summary.at("return_type"), classPaths);
		if (!returnType || (summary.at("purity").get<int>() == static_cast<int>(InstantiationPurity::Pure) &&
							returnType->kind != DataType::Kind::Void && !allowPureValueSummary))
			return false;
		bool needsGlobalTypes = summary.at("needs_global_types").get<bool>();
		if (needsGlobalTypes) {
			auto currentGlobalTypes = captureGlobalTypeProjection(sectionPaths);
			if (!currentGlobalTypes || *currentGlobalTypes != priorGlobalTypes)
				return false;
		}
		if (!irReuse.hasReusableIRForInstantiation(section, *keyText, needsGlobalTypes))
			return false;
		if (!storage.canDecodeProvenance(summary.at("return_provenance")) ||
			!canDecodeGlobalEffects(summary.at("global_effects"), storage))
			return false;
		instantiation.returnType = std::move(*returnType);
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
		decodePureReturnValues(summary.at("pure_values"), instantiation);
		instantiation.inferenceReusedFromCache = true;
		count++;
		return true;
	}

	size_t reusedCount() const { return count; }
	Json snapshot(
		const std::map<const Section *, std::string> &paths, const std::set<std::string> &excludedSources,
		const Json &globalTypes, size_t *reusedSections = nullptr
	) const {
		std::unordered_set<const Section *> skipped;
		if (globalTypes == priorGlobalTypes)
			for (const Section *section : eligible)
				if (std::ranges::all_of(section->instantiations, [](const auto &entry) {
					return entry.second.inferenceReusedFromCache;
				}))
					skipped.insert(section);
		Json result = captureInferenceSummaries(context, paths, excludedSources, globalTypes, &skipped);
		for (const Section *section : eligible) {
			if (inferenceSummarySourceExcluded(section, excludedSources))
				continue;
			const std::string &path = sectionPaths.at(section);
			if (summaries.contains(path)) {
				bool copied = false;
				for (const auto &[key, summary] : summaries.at(path).items()) {
					if (summary.at("needs_global_types").get<bool>() && globalTypes != priorGlobalTypes)
						continue;
					if (!result[path].contains(key)) {
						result[path][key] = summary;
						copied = true;
					}
				}
				if (copied && skipped.contains(section) && reusedSections)
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
	std::unordered_set<const Section *> eligible;
	const std::map<const Section *, std::string> &sectionPaths;
	ClassIdentityPaths classPaths;
	InferenceStorageIdentities storage;
	size_t count = 0;
};
