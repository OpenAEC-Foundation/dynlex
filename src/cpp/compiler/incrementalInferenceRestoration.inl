// Intern symbolic references first. No ordinary inference runs while a record
// graph is being decoded, so cycles cannot observe half-restored effects.
class Restoration {
  public:
	Restoration(IncrementalInferenceReuse &owner, InferenceContext *caller) : owner(owner), caller(caller) {
		requireCompilerInvariant(!owner.restoration, "nested inference record restoration");
		owner.restoration = this;
	}
	~Restoration() {
		if (!committed) {
			for (auto &[slot, entry] : entries)
				*slot = std::move(entry.original);
			for (const auto &[definition, size] : classSizes)
				definition->instantiations.resize(size);
			for (const auto &[definition, key] : classRequests)
				definition->instantiationRequests.erase(key);
		}
		owner.restoration = nullptr;
	}

	bool prepare(Section *section, const InstantiationKey &key, Instantiation &slot, ParseContext::InferenceReuseUse use) {
		auto identity = inferenceSummaryKey(key, owner.identities);
		if (!identity ||
			(!owner.eligible->contains(section, *identity) && !owner.refreshed(owner.sectionPaths.at(section), *identity)))
			return false;
		const std::string &path = owner.sectionPaths.at(section);
		const Json *reference = owner.reference({path, *identity});
		if (!reference)
			return false;
		const Json *saved = owner.summaryStore.load(*reference);
		if (!saved)
			return false;
		Entry &entry = stage(section, key, slot);
		if (entry.ready)
			return true;
		entry.identity = *identity;
		const Json &summary = *saved;
		if (!types(summary, *reference))
			return false;
		if (!owner.globalInputClosureUnchanged(path, *identity))
			return false;
		auto invocation = decodeInferenceInvocation(*section, summary.at("invocation"));
		entry.facts.invokedDefinition = invocation.definition;
		entry.facts.argumentNames = std::move(invocation.names);
		entry.facts.requiredCompileTimeParameters = std::move(invocation.requiredCompileTime);
		if (!summary.at("failure").is_null()) {
			Instantiation &facts = entry.facts;
			facts.argumentTypes = key.argumentTypes;
			facts.storageScope = owner.context.storageScopeForInstantiation(section, key);
			owner.globalInputs.retain(facts.storageScope, summary.at("global_inputs"));
			if (!owner.bodySources)
				owner.bodySources = std::make_unique<InferenceBodySources>(owner.context, owner.identities);
			facts.failure =
				restoreInferenceFailure(owner.context, summary.at("failure"), *owner.bodySources, facts.storageScope);
			facts.valid = false;
			facts.inferenceReusedFromCache = true;
			entry.ready = true;
			return true;
		}
		auto returnType = decodeInferenceTypeIdentity(summary.at("return_type"), owner.identities.classes);
		if (!returnType)
			return false;
		// This signature is visible only to the record decoder. valid remains
		// false until every referenced unit has complete, validated facts.
		slot.argumentTypes = key.argumentTypes;
		slot.returnType = *returnType;
		slot.valid = false;
		auto parameters = decodeInferenceParameters(summary.at("parameters"), owner.identities);
		auto values = decodePureReturnValues(summary.at("pure_values"), owner.identities);
		if (!parameters || !values || !owner.storage.canDecodeProvenance(summary.at("return_provenance")) ||
			!canDecodeGlobalEffects(summary.at("global_effects"), owner.storage))
			return false;
		Instantiation &facts = entry.facts;
		facts.argumentTypes = key.argumentTypes;
		facts.returnType = std::move(*returnType);
		applyInferenceParameters(std::move(*parameters), facts);
		facts.purity = static_cast<InstantiationPurity>(summary.at("purity").get<int>());
		facts.hasReturnAddressProvenance = summary.at("has_return_address_provenance").get<bool>();
		facts.returnAddressProvenance = owner.storage.decodeProvenance(summary.at("return_provenance"));
		facts.returnPointerStorageParameterName =
			summary.at("return_storage_parameter").is_null()
				? std::nullopt
				: std::optional<std::string>{summary.at("return_storage_parameter").get<std::string>()};
		facts.returnPointerStorageAmbiguous = summary.at("return_storage_ambiguous").get<bool>();
		facts.fallsThrough = summary.at("falls_through").get<bool>();
		facts.hasReturnIntrinsic = summary.at("return_intrinsic").get<bool>();
		facts.writesThroughUnknownAddress = summary.at("unknown_write").get<bool>();
		facts.externallyEscapesUnknownAddress = summary.at("unknown_escape").get<bool>();
		decodeGlobalEffects(summary.at("global_effects"), facts, owner.storage);
		facts.pureReturnValuesByArguments = std::move(*values);
		facts.pureReturnValuesComplete = summary.at("pure_values_complete").get<bool>();
		facts.body = entry.original.body;
		facts.storageScope = owner.context.storageScopeForInstantiation(section, key);
		owner.globalInputs.retain(facts.storageScope, summary.at("global_inputs"));
		bool pureValue = facts.purity == InstantiationPurity::Pure && facts.returnType.kind != DataType::Kind::Void;
		bool requiresBody = use != ParseContext::InferenceReuseUse::SymbolicReference &&
							(use != ParseContext::InferenceReuseUse::ConstantCall || !pureValue);
		entry.hasIR = requiresBody && owner.irReuse && owner.irReuse->hasReusableIRForInstantiation(section, *identity);
		facts.inferenceReusedFromCache = true;
		facts.needsReinfer = requiresBody && !entry.hasIR;
		entry.ready = true;
		return true;
	}

	std::optional<CompileTimeFunction> function(const Json &encoded) {
		auto found = owner.identities.sections.find(encoded.at(1).get<std::string>());
		if (found == owner.identities.sections.end())
			return std::nullopt;
		Section *section = found->second;
		size_t definitionIndex = encoded.at(2).get<size_t>(), pathIndex = encoded.at(3).get<size_t>();
		if (section->type != SectionType::Function || section->isFlex || definitionIndex >= section->patternDefinitions.size())
			return std::nullopt;
		PatternDefinition *definition = section->patternDefinitions.at(definitionIndex);
		if (pathIndex >= definition->indexedPaths.size())
			return std::nullopt;
		InstantiationKey key;
		for (const Json &argument : encoded.at(4)) {
			auto type = decodeInferenceTypeIdentity(argument, owner.identities.classes);
			if (!type)
				return std::nullopt;
			key.argumentTypes.push_back(std::move(*type));
		}
		auto result = decodeInferenceTypeIdentity(encoded.at(5), owner.identities.classes);
		if (!result)
			return std::nullopt;
		std::vector<CallableFunctionParameter> parameters;
		collectCallableFunctionParameters({definition, pathIndex}, parameters);
		if (parameters.size() != key.argumentTypes.size())
			return std::nullopt;
		for (size_t index = 0; index < parameters.size(); index++)
			if (parameters[index].requiresCompileTimeValue)
				return std::nullopt;
		recordInferenceRestoration(caller, section, key);
		Instantiation &slot = section->instantiations[key];
		if (!entries.contains(&slot)) {
			if (slot.inferring)
				return std::nullopt;
			if (!slot.valid || !slot.returnType.isDeduced() || slot.needsReinfer) {
				stage(section, key, slot);
				slot.argumentTypes = key.argumentTypes;
				slot.returnType = *result;
				slot.valid = false;
			}
		}
		references.push_back({section, key, &slot, std::move(*result)});
		CompileTimeFunction function{definition, pathIndex, &slot};
		functionIdentities[function] = encoded;
		return function;
	}

#include "incrementalInferenceLayoutRestoration.inl"

	std::optional<Json> identity(const CompileTimeFunction &function) const {
		auto found = functionIdentities.find(function);
		return found == functionIdentities.end() ? std::nullopt : std::optional<Json>{found->second};
	}
	Instantiation *instance(Section *section, const InstantiationKey &key) {
		auto found = section->instantiations.find(key);
		if (found != section->instantiations.end() && found->second.valid && !found->second.inferring &&
			found->second.returnType.isDeduced() && !found->second.needsReinfer)
			return &found->second;
		recordInferenceRestoration(caller, section, key);
		auto &slot = section->instantiations[key];
		if (!prepare(section, key, slot, ParseContext::InferenceReuseUse::SymbolicReference))
			return nullptr;
		return &slot;
	}

	bool finish() {
		for (size_t index = 0; index < references.size(); index++) {
			Reference reference = references[index];
			auto pending = entries.find(reference.slot);
			if (pending != entries.end() && !pending->second.ready &&
				!prepare(reference.section, reference.key, *reference.slot, ParseContext::InferenceReuseUse::SymbolicReference))
				return false;
			const Instantiation &facts = pending == entries.end() ? *reference.slot : pending->second.facts;
			if (facts.argumentTypes != reference.key.argumentTypes || facts.returnType != reference.result)
				return false;
		}
		for (auto &[slot, entry] : entries) {
			requireCompilerInvariant(entry.ready, "inference restoration left an undecoded unit");
			*slot = std::move(entry.facts);
			restoreInferenceDiagnostics(
				owner.context, slot->storageScope,
				owner.reference({owner.sectionPaths.at(entry.section), entry.identity})->at("diagnostics"), [&]() -> auto & {
				if (!owner.bodySources)
					owner.bodySources = std::make_unique<InferenceBodySources>(owner.context, owner.identities);
				return *owner.bodySources;
			}
			);
			if (entry.hasIR)
				owner.runtimeValidated.emplace(entry.section, entry.identity);
			auto key = std::pair{owner.sectionPaths.at(entry.section), entry.identity};
			bool current = owner.refreshed(key.first, key.second);
			if (slot->failure && !current)
				owner.context.inferenceFailuresReused++;
			(current ? owner.currentCount : owner.count)++;
			slot->inferenceProductIdentity = owner.productIdentity(key);
			const Json *prior = current ? nullptr : &owner.units.at(key.first).at(key.second);
			owner.retainRestored(
				key, slot->storageScope, *owner.reference(key), *owner.inputProof(key), prior, slot->inferenceProductIdentity,
				owner.selection(key)
			);
		}
		committed = true;
		return true;
	}

  private:
	struct Entry {
		Section *section;
		Instantiation original;
		Instantiation facts;
		std::string identity;
		bool ready = false;
		bool hasIR = false;
	};
	struct Reference {
		Section *section;
		InstantiationKey key;
		Instantiation *slot;
		DataType result;
	};
	Entry &stage(Section *section, const InstantiationKey &key, Instantiation &slot) {
		if (auto found = entries.find(&slot); found != entries.end())
			return found->second;
		recordInferenceRestoration(caller, section, key);
		return entries.emplace(&slot, Entry{section, slot, {}, {}, false, false}).first->second;
	}
	IncrementalInferenceReuse &owner;
	InferenceContext *caller;
	std::map<Instantiation *, Entry> entries;
	std::map<CompileTimeFunction, Json> functionIdentities;
	std::map<ClassDefinition *, size_t> classSizes;
	std::vector<std::pair<ClassDefinition *, std::string>> classRequests;
	std::vector<Reference> references;
	bool committed = false;
};
