std::optional<std::string> globalEffectName(const Section *section, const VariableReference *reference) {
	for (const Section *scope = section; scope; scope = scope->parent) {
		auto found = scope->variables.find(reference->name);
		if (found != scope->variables.end())
			return found->second->definition == reference && found->second->isGlobal
					   ? std::optional<std::string>{reference->name}
					   : std::nullopt;
	}
	return std::nullopt;
}

VariableReference *globalEffectReference(Section *section, const std::string &name) {
	Variable *variable = section->findVariable(name);
	requireCompilerInvariant(variable && variable->isGlobal && variable->definition, "cached global effect lost its variable");
	return variable->definition;
}

#include "incrementalInferenceStorage.inl"

std::optional<Json>
encodeGlobalEffects(const Section *section, const Instantiation &instantiation, InferenceStorageIdentities &storage) {
	Json writes = Json::object();
	for (VariableReference *reference : instantiation.writtenGlobalReferences) {
		auto name = globalEffectName(section, reference);
		if (!name)
			return std::nullopt;
		auto found = instantiation.finalGlobalAddressProvenance.find(reference);
		AddressProvenance provenance = found == instantiation.finalGlobalAddressProvenance.end()
										   ? AddressProvenance{.mayTargets = {}, .unknown = true}
										   : found->second;
		auto encoded = storage.encodeProvenance(provenance);
		if (!encoded)
			return std::nullopt;
		writes[*name] = std::move(*encoded);
	}
	for (const auto &[reference, _] : instantiation.finalGlobalAddressProvenance) {
		if (!instantiation.writtenGlobalReferences.contains(reference))
			return std::nullopt;
	}
	Json constants = Json::object();
	for (const auto &[reference, value] : instantiation.finalGlobalConstantValues) {
		if (!instantiation.writtenGlobalReferences.contains(reference))
			return std::nullopt;
		auto name = globalEffectName(section, reference);
		auto encoded = storage.encodeValue(value);
		if (!name || !encoded)
			return std::nullopt;
		constants[*name] = std::move(*encoded);
	}
	std::set<std::string> taken;
	for (VariableReference *reference : instantiation.addressTakenGlobalReferences) {
		auto name = globalEffectName(section, reference);
		if (!name)
			return std::nullopt;
		taken.insert(*name);
	}
	auto escaped = storage.encodeProvenance(instantiation.externallyEscapedGlobalProvenance);
	if (!escaped)
		return std::nullopt;
	return Json{{"writes", writes}, {"constants", constants}, {"taken", taken}, {"escaped", std::move(*escaped)}};
}

bool canDecodeGlobalEffects(const Json &encoded, const InferenceStorageIdentities &storage) {
	return std::ranges::all_of(
			   encoded.at("constants"),
			   [&](const Json &value) {
		return storage.canDecodeValue(value);
	}
		   ) &&
		   storage.canDecodeProvenance(encoded.at("escaped")) &&
		   std::ranges::all_of(encoded.at("writes"), [&](const Json &provenance) {
		return storage.canDecodeProvenance(provenance);
	});
}

void decodeGlobalEffects(
	Section *section, const Json &encoded, Instantiation &instantiation, InferenceStorageIdentities &storage
) {
	for (const auto &[name, provenance] : encoded.at("writes").items()) {
		VariableReference *reference = globalEffectReference(section, name);
		instantiation.writtenGlobalReferences.insert(reference);
		instantiation.finalGlobalAddressProvenance.emplace(reference, storage.decodeProvenance(provenance));
	}
	for (const auto &[name, value] : encoded.at("constants").items())
		instantiation.finalGlobalConstantValues.emplace(globalEffectReference(section, name), storage.decodeValue(value));
	for (const std::string &name : encoded.at("taken").get<std::set<std::string>>())
		instantiation.addressTakenGlobalReferences.insert(globalEffectReference(section, name));
	instantiation.externallyEscapedGlobalProvenance = storage.decodeProvenance(encoded.at("escaped"));
}
