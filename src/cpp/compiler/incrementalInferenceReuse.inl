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
		if (type.pointerDepth != 0 || (type.kind != DataType::Kind::Bool && type.kind != DataType::Kind::Float &&
									   type.kind != DataType::Kind::Int && type.kind != DataType::Kind::UInt))
			return std::nullopt;
		result += std::to_string(static_cast<int>(type.kind)) + ":" + std::to_string(type.numericSize) + ";";
	}
	return result;
}

bool hasScalarInferenceResult(const DataType &type) {
	return type.pointerDepth == 0 &&
		   (type.kind == DataType::Kind::Void || type.kind == DataType::Kind::Bool || type.kind == DataType::Kind::Float ||
			type.kind == DataType::Kind::Int || type.kind == DataType::Kind::UInt);
}

bool canPersistInferenceSummary(const Section *section, const InstantiationKey &key, const Instantiation &instantiation) {
	return section->type == SectionType::Function && !section->isFlex && !section->isConversion && hasNoGlobalAccess(section) &&
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
		   instantiation.pureReturnValuesByArguments.empty() && instantiation.writtenGlobalReferences.empty() &&
		   instantiation.finalGlobalConstantValues.empty() && instantiation.finalGlobalAddressProvenance.empty() &&
		   instantiation.addressTakenGlobalReferences.empty() &&
		   instantiation.externallyEscapedGlobalProvenance == AddressProvenance{} &&
		   instantiation.returnAddressProvenance == AddressProvenance{} && !instantiation.returnPointerStorageParameterName &&
		   !instantiation.returnPointerStorageAmbiguous && !instantiation.memoryViolation;
}

Json captureInferenceSummaries(const std::map<const Section *, std::string> &sectionPaths) {
	Json summaries = Json::object();
	for (const auto &[section, path] : sectionPaths) {
		for (const auto &[key, instantiation] : section->instantiations) {
			if (!canPersistInferenceSummary(section, key, instantiation))
				continue;
			summaries[path][*inferenceSummaryKey(key)] = Json{
				{"return_kind", static_cast<int>(instantiation.returnType.kind)},
				{"return_size", instantiation.returnType.numericSize},
				{"has_return_address_provenance", instantiation.hasReturnAddressProvenance},
				{"falls_through", instantiation.fallsThrough},
				{"return_intrinsic", instantiation.hasReturnIntrinsic},
				{"unknown_write", instantiation.writesThroughUnknownAddress},
				{"unknown_escape", instantiation.externallyEscapesUnknownAddress}
			};
		}
	}
	return summaries;
}

class IncrementalInferenceReuse {
  public:
	IncrementalInferenceReuse(
		const ParseContext &context, const Json &summaries, const IncrementalIRReuse &irReuse,
		std::unordered_set<const Section *> eligible
	)
		: summaries(summaries), irReuse(irReuse), eligible(std::move(eligible)) {
		std::map<std::string, Section *> sections;
		collectSections(context.mainSection, "root", sections);
		for (const auto &[path, section] : sections)
			sectionPaths.emplace(section, path);
	}

	bool reuse(Section *section, const InstantiationKey &key, Instantiation &instantiation) {
		std::optional<std::string> keyText = inferenceSummaryKey(key);
		if (!eligible.contains(section) || !keyText || !hasNoGlobalAccess(section) || !irReuse.hasReusableIRForSection(section))
			return false;
		const std::string &path = sectionPaths.at(section);
		if (!summaries.contains(path) || !summaries.at(path).contains(*keyText))
			return false;
		const Json &summary = summaries.at(path).at(*keyText);
		instantiation.returnType =
			DataType{static_cast<DataType::Kind>(summary.at("return_kind").get<int>()), summary.at("return_size").get<int>()};
		instantiation.purity = InstantiationPurity::Impure;
		instantiation.hasReturnAddressProvenance = summary.at("has_return_address_provenance").get<bool>();
		instantiation.fallsThrough = summary.at("falls_through").get<bool>();
		instantiation.hasReturnIntrinsic = summary.at("return_intrinsic").get<bool>();
		instantiation.writesThroughUnknownAddress = summary.at("unknown_write").get<bool>();
		instantiation.externallyEscapesUnknownAddress = summary.at("unknown_escape").get<bool>();
		count++;
		return true;
	}

	size_t reusedCount() const { return count; }
	Json snapshot(const std::map<const Section *, std::string> &paths) const {
		Json result = captureInferenceSummaries(paths);
		for (const Section *section : eligible) {
			const std::string &path = sectionPaths.at(section);
			if (summaries.contains(path)) {
				for (const auto &[key, summary] : summaries.at(path).items()) {
					if (!result[path].contains(key))
						result[path][key] = summary;
				}
			}
		}
		return result;
	}

  private:
	const Json &summaries;
	const IncrementalIRReuse &irReuse;
	std::unordered_set<const Section *> eligible;
	std::map<const Section *, std::string> sectionPaths;
	size_t count = 0;
};
