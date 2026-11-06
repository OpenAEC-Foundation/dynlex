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
	return section->type == SectionType::Function && !section->isFlex && !section->isConversion &&
		   inferenceSummaryKey(key, classPaths).has_value() && instantiation.valid && !instantiation.inferring &&
		   !instantiation.needsReinfer && instantiation.body &&
		   inferenceTypeIdentity(instantiation.returnType, classPaths, visiting).has_value() &&
		   instantiation.purity == InstantiationPurity::Impure && instantiation.argumentTypes == key.argumentTypes &&
		   instantiation.parameterSeedTypesByName.empty() &&
		   std::ranges::all_of(
			   instantiation.parameterOutputTypesByName,
			   [&](const auto &entry) {
		auto input = instantiation.parameterTypesByName.find(entry.first);
		return input != instantiation.parameterTypesByName.end() && input->second == entry.second;
	}
		   ) &&
		   instantiation.constantParameterValues.empty() && instantiation.requiredCompileTimeParameters.empty() &&
		   instantiation.pureReturnValuesByArguments.empty() &&
		   !instantiation.memoryViolation;
}

bool inferenceSummarySourceExcluded(const Section *section, const std::set<std::string> &excludedSources) {
	return !section->openingLine || excludedSources.contains(section->openingLine->sourceFile->uri);
}

Json captureInferenceSummaries(
	const std::map<const Section *, std::string> &sectionPaths, const std::set<std::string> &excludedSources,
	const Json &globalTypes
) {
	Json summaries = Json::object();
	ClassIdentityPaths classPaths = classIdentityPaths(sectionPaths);
	for (const auto &[section, path] : sectionPaths) {
		if (inferenceSummarySourceExcluded(section, excludedSources))
			continue;
		for (const auto &[key, instantiation] : section->instantiations) {
			if (!canPersistInferenceSummary(section, key, instantiation, classPaths))
				continue;
			auto globalEffects = encodeGlobalEffects(section, instantiation);
			if (!globalEffects)
				continue;
			auto returnProvenance = encodeAddressProvenance(section, instantiation.returnAddressProvenance);
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
			summaries[path][*inferenceSummaryKey(key, classPaths)] = Json{
				{"argument_types", std::move(argumentTypes)},
				{"return_type", *inferenceTypeIdentity(instantiation.returnType, classPaths, visiting)},
				{"has_return_address_provenance", instantiation.hasReturnAddressProvenance},
				{"return_provenance", std::move(*returnProvenance)},
				{"return_storage_parameter", instantiation.returnPointerStorageParameterName
					? Json(*instantiation.returnPointerStorageParameterName) : Json(nullptr)},
				{"return_storage_ambiguous", instantiation.returnPointerStorageAmbiguous},
				{"falls_through", instantiation.fallsThrough},
				{"return_intrinsic", instantiation.hasReturnIntrinsic},
				{"unknown_write", instantiation.writesThroughUnknownAddress},
				{"unknown_escape", instantiation.externallyEscapesUnknownAddress},
				{"needs_global_types", needsGlobalTypes},
				{"global_effects", std::move(*globalEffects)}
			};
		}
	}
	return summaries;
}

class IncrementalInferenceReuse {
  public:
	IncrementalInferenceReuse(
		const ParseContext &context, const Json &summaries, const Json &priorGlobalTypes, const IncrementalIRReuse &irReuse,
		std::unordered_set<const Section *> eligible
	)
		: summaries(summaries), priorGlobalTypes(priorGlobalTypes), irReuse(irReuse), eligible(std::move(eligible)) {
		std::map<std::string, Section *> sections;
		collectSections(context.mainSection, "root", sections);
		for (const auto &[path, section] : sections)
			sectionPaths.emplace(section, path);
		classPaths = classIdentityPaths(sectionPaths);
	}

	bool reuse(Section *section, const InstantiationKey &key, Instantiation &instantiation) {
		std::optional<std::string> keyText = inferenceSummaryKey(key, classPaths);
		if (!eligible.contains(section) || !keyText)
			return false;
		const std::string &path = sectionPaths.at(section);
		if (!summaries.contains(path) || !summaries.at(path).contains(*keyText))
			return false;
		const Json &summary = summaries.at(path).at(*keyText);
		bool needsGlobalTypes = summary.at("needs_global_types").get<bool>();
		if (needsGlobalTypes) {
			auto currentGlobalTypes = captureGlobalTypeProjection(sectionPaths);
			if (!currentGlobalTypes || *currentGlobalTypes != priorGlobalTypes)
				return false;
		}
		if (!irReuse.hasReusableIRForSection(section, needsGlobalTypes))
			return false;
		auto returnType = decodeInferenceTypeIdentity(summary.at("return_type"), classPaths);
		if (!returnType)
			return false;
		instantiation.returnType = std::move(*returnType);
		instantiation.purity = InstantiationPurity::Impure;
		instantiation.hasReturnAddressProvenance = summary.at("has_return_address_provenance").get<bool>();
		instantiation.returnAddressProvenance = decodeAddressProvenance(section, summary.at("return_provenance"));
		instantiation.returnPointerStorageParameterName = summary.at("return_storage_parameter").is_null()
			? std::nullopt
			: std::optional<std::string>{summary.at("return_storage_parameter").get<std::string>()};
		instantiation.returnPointerStorageAmbiguous = summary.at("return_storage_ambiguous").get<bool>();
		instantiation.fallsThrough = summary.at("falls_through").get<bool>();
		instantiation.hasReturnIntrinsic = summary.at("return_intrinsic").get<bool>();
		instantiation.writesThroughUnknownAddress = summary.at("unknown_write").get<bool>();
		instantiation.externallyEscapesUnknownAddress = summary.at("unknown_escape").get<bool>();
		decodeGlobalEffects(section, summary.at("global_effects"), instantiation);
		instantiation.inferenceReusedFromCache = true;
		count++;
		return true;
	}

	size_t reusedCount() const { return count; }
	Json snapshot(
		const std::map<const Section *, std::string> &paths, const std::set<std::string> &excludedSources,
		const Json &globalTypes
	) const {
		Json result = captureInferenceSummaries(paths, excludedSources, globalTypes);
		for (const Section *section : eligible) {
			if (inferenceSummarySourceExcluded(section, excludedSources))
				continue;
			const std::string &path = sectionPaths.at(section);
			if (summaries.contains(path)) {
				for (const auto &[key, summary] : summaries.at(path).items()) {
					if (summary.at("needs_global_types").get<bool>() && globalTypes != priorGlobalTypes)
						continue;
					if (!result[path].contains(key))
						result[path][key] = summary;
				}
			}
		}
		return result;
	}

  private:
	const Json &summaries;
	const Json &priorGlobalTypes;
	const IncrementalIRReuse &irReuse;
	std::unordered_set<const Section *> eligible;
	std::map<const Section *, std::string> sectionPaths;
	ClassIdentityPaths classPaths;
	size_t count = 0;
};
