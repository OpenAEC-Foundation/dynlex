// Global type inputs belong to concrete queries, independently of the other
// globals in the program. The index owns no type snapshots: inference and class
// restoration can still change the current types between queries.
bool mergeInferenceGlobalInputs(Json &current, const Json &inputs) {
	if (current.is_null())
		return false;
	if (inputs.is_null()) {
		current = nullptr;
		return true;
	}
	bool changed = false;
	for (const auto &[definition, type] : inputs.items()) {
		if (!current.contains(definition)) {
			current[definition] = type;
			changed = true;
		} else if (current.at(definition) != type) {
			current = nullptr; // These queries require different global type contexts.
			return true;
		}
	}
	return changed;
}

class InferenceGlobalInputs {
  public:
	InferenceGlobalInputs(
		const std::map<const Section *, std::string> &paths, const InferenceSourceIndex &sources, BuildStatistics &statistics
	)
		: paths(paths), sources(sources), statistics(statistics), classes(classIdentityPaths(paths)) {}
	using Scope = std::shared_ptr<const VariableStorageScope>;
	void observe(const InferenceReadOwner &owner, VariableReference *reference) {
		if (owner.section && observations[owner.scope].insert(normalizeBindingReference(reference)).second)
			statistics.globalTypeInputReadsRecorded++;
	}
	void retain(const Scope &scope, const Json &inputs) {
		for (const auto &[definition, type] : inputs.items()) {
			prepare();
			observations[scope].insert(normalizeBindingReference(variables.at(definition)->definition));
		}
	}
	std::optional<Json> captureScope(const Scope &scope) {
		auto found = observations.find(scope);
		return captureReferences(found == observations.end() ? std::unordered_set<VariableReference *>{} : found->second);
	}

	std::optional<Json> capture(const Instantiation &instance) {
		std::unordered_set<VariableReference *> required = instance.accessedGlobalReferences;
		if (auto found = observations.find(instance.storageScope); found != observations.end())
			required.insert(found->second.begin(), found->second.end());
		required.insert(instance.writtenGlobalReferences.begin(), instance.writtenGlobalReferences.end());
		required.insert(instance.addressTakenGlobalReferences.begin(), instance.addressTakenGlobalReferences.end());
		auto provenance = [&](const AddressProvenance &value) {
			for (const auto &target : value.mayTargets) {
				prepare();
				if (globals.contains(normalizeBindingReference(target.definition)))
					required.insert(normalizeBindingReference(target.definition));
			}
		};
		provenance(instance.returnAddressProvenance);
		provenance(instance.externallyEscapedGlobalProvenance);
		for (const auto &[reference, value] : instance.finalGlobalAddressProvenance) {
			required.insert(reference);
			provenance(value);
		}
		return captureReferences(required);
	}

	bool unchanged(const Json &inputs) {
		if (inputs.is_null())
			return false;
		for (const auto &[definition, prior] : inputs.items()) {
			prepare();
			statistics.globalTypeInputsChecked++;
			auto found = variables.find(definition);
			if (found == variables.end())
				return false;
			auto current = fingerprint(*found->second);
			if (!current || *current != prior.get_ref<const std::string &>())
				return false;
		}
		return true;
	}

  private:
	std::optional<Json> captureReferences(const std::unordered_set<VariableReference *> &required) {
		Json inputs = Json::object();
		for (VariableReference *reference : required) {
			prepare();
			Variable *variable = globals.at(normalizeBindingReference(reference));
			auto type = fingerprint(*variable);
			if (!type)
				return std::nullopt;
			inputs[sources.definition(normalizeBindingReference(reference)).dump()] = std::move(*type);
			statistics.globalTypeInputsCaptured++;
		}
		return inputs;
	}

	void prepare() {
		if (prepared)
			return;
		std::map<std::string, const Section *> sections;
		for (const auto &[section, path] : paths)
			sections.emplace(path, section);
		for (const auto &[section, path] : paths)
			for (const auto &[name, variable] : section->variables) {
				if (!variable->isGlobal)
					continue;
				auto *reference = normalizeBindingReference(variable->definition);
				const Json &definition = sources.definition(reference);
				Variable *canonical =
					sections.at(definition.at(0).get<std::string>())->variables.at(definition.at(1).get<std::string>());
				requireCompilerInvariant(canonical->isGlobal, "global input has a nonglobal declaration");
				globals.emplace(reference, canonical);
				variables.emplace(definition.dump(), canonical);
			}
		statistics.globalInputDefinitionsIndexed += variables.size();
		prepared = true;
	}
	std::optional<std::string> fingerprint(const Variable &variable) {
		std::set<std::pair<const ClassDefinition *, int>> visiting;
		auto type = inferenceTypeIdentity(variable.type, classes, visiting);
		statistics.globalTypeInputFingerprintsComputed++;
		return type ? std::optional<std::string>{digest(type->dump())} : std::nullopt;
	}
	const std::map<const Section *, std::string> &paths;
	const InferenceSourceIndex &sources;
	BuildStatistics &statistics;
	ClassIdentityPaths classes;
	bool prepared = false;
	std::unordered_map<const VariableReference *, Variable *> globals;
	std::map<std::string, Variable *> variables;
	std::map<Scope, std::unordered_set<VariableReference *>, std::owner_less<Scope>> observations;
};
