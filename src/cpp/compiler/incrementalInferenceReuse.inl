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

std::optional<std::string> inferenceSummaryKey(const InstantiationKey &key) {
	if (!key.compileTimeParameters.empty())
		return std::nullopt;
	std::string result;
	for (const DataType &type : key.argumentTypes) {
		if (type.kind != DataType::Kind::Bool && type.kind != DataType::Kind::Float && type.kind != DataType::Kind::Int &&
			type.kind != DataType::Kind::UInt)
			return std::nullopt;
		result += std::to_string(static_cast<int>(type.kind)) + ":" + std::to_string(type.numericSize) + ":" +
				  std::to_string(type.pointerDepth) + ";";
	}
	return result;
}

bool hasScalarInferenceResult(const DataType &type) {
	return type.pointerDepth == 0 &&
		   (type.kind == DataType::Kind::Void || type.kind == DataType::Kind::Bool || type.kind == DataType::Kind::Float ||
			type.kind == DataType::Kind::Int || type.kind == DataType::Kind::UInt);
}

bool canPersistInferenceSummary(const Section *section, const InstantiationKey &key, const Instantiation &instantiation) {
	return section->type == SectionType::Function && !section->isFlex && !section->isConversion &&
		   inferenceSummaryKey(key).has_value() && instantiation.valid && !instantiation.inferring &&
		   !instantiation.needsReinfer && instantiation.body && hasScalarInferenceResult(instantiation.returnType) &&
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
		   instantiation.pureReturnValuesByArguments.empty() && instantiation.returnAddressProvenance == AddressProvenance{} &&
		   !instantiation.returnPointerStorageParameterName && !instantiation.returnPointerStorageAmbiguous &&
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
	for (const auto &[section, path] : sectionPaths) {
		if (inferenceSummarySourceExcluded(section, excludedSources))
			continue;
		for (const auto &[key, instantiation] : section->instantiations) {
			if (!canPersistInferenceSummary(section, key, instantiation))
				continue;
			auto globalEffects = encodeGlobalEffects(section, instantiation);
			if (!globalEffects)
				continue;
			bool needsGlobalTypes = !hasNoGlobalAccess(section) || !instantiation.writtenGlobalReferences.empty() ||
									!instantiation.addressTakenGlobalReferences.empty() ||
									instantiation.externallyEscapedGlobalProvenance != AddressProvenance{};
			if (needsGlobalTypes && globalTypes.is_null())
				continue;
			summaries[path][*inferenceSummaryKey(key)] = Json{
				{"return_kind", static_cast<int>(instantiation.returnType.kind)},
				{"return_size", instantiation.returnType.numericSize},
				{"has_return_address_provenance", instantiation.hasReturnAddressProvenance},
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
	}

	bool reuse(Section *section, const InstantiationKey &key, Instantiation &instantiation) {
		std::optional<std::string> keyText = inferenceSummaryKey(key);
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
		instantiation.returnType =
			DataType{static_cast<DataType::Kind>(summary.at("return_kind").get<int>()), summary.at("return_size").get<int>()};
		instantiation.purity = InstantiationPurity::Impure;
		instantiation.hasReturnAddressProvenance = summary.at("has_return_address_provenance").get<bool>();
		instantiation.fallsThrough = summary.at("falls_through").get<bool>();
		instantiation.hasReturnIntrinsic = summary.at("return_intrinsic").get<bool>();
		instantiation.writesThroughUnknownAddress = summary.at("unknown_write").get<bool>();
		instantiation.externallyEscapesUnknownAddress = summary.at("unknown_escape").get<bool>();
		decodeGlobalEffects(section, summary.at("global_effects"), instantiation);
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
	size_t count = 0;
};
