#include "incrementalInferenceStorage.inl"

std::optional<Json> encodeGlobalEffects(const Instantiation &instantiation, InferenceStorageIdentities &storage) {
	std::set<Json> accessed;
	for (VariableReference *reference : instantiation.accessedGlobalReferences)
		accessed.insert(storage.encodeDefinition(reference).value());
	Json writes = Json::object();
	for (VariableReference *reference : instantiation.writtenGlobalReferences) {
		auto definition = storage.encodeDefinition(reference);
		if (!definition)
			return std::nullopt;
		auto found = instantiation.finalGlobalAddressProvenance.find(reference);
		AddressProvenance provenance = found == instantiation.finalGlobalAddressProvenance.end()
										   ? AddressProvenance{.mayTargets = {}, .unknown = true}
										   : found->second;
		auto encoded = storage.encodeProvenance(provenance);
		if (!encoded)
			return std::nullopt;
		writes[definition->dump()] = std::move(*encoded);
	}
	for (const auto &[reference, _] : instantiation.finalGlobalAddressProvenance) {
		if (!instantiation.writtenGlobalReferences.contains(reference))
			return std::nullopt;
	}
	Json constants = Json::object();
	for (const auto &[reference, value] : instantiation.finalGlobalConstantValues) {
		if (!instantiation.writtenGlobalReferences.contains(reference))
			return std::nullopt;
		auto definition = storage.encodeDefinition(reference);
		auto encoded = storage.encodeValue(value);
		if (!definition || !encoded)
			return std::nullopt;
		constants[definition->dump()] = std::move(*encoded);
	}
	std::set<Json> taken;
	for (VariableReference *reference : instantiation.addressTakenGlobalReferences) {
		auto definition = storage.encodeDefinition(reference);
		if (!definition)
			return std::nullopt;
		taken.insert(std::move(*definition));
	}
	auto escaped = storage.encodeProvenance(instantiation.externallyEscapedGlobalProvenance);
	if (!escaped)
		return std::nullopt;
	return Json{
		{"accessed", accessed}, {"writes", writes}, {"constants", constants}, {"taken", taken}, {"escaped", std::move(*escaped)}
	};
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

void decodeGlobalEffects(const Json &encoded, Instantiation &instantiation, InferenceStorageIdentities &storage) {
	for (const Json &definition : encoded.at("accessed"))
		instantiation.accessedGlobalReferences.insert(storage.decodeDefinition(definition));
	for (const auto &[name, provenance] : encoded.at("writes").items()) {
		VariableReference *reference = storage.decodeDefinition(Json::parse(name));
		instantiation.writtenGlobalReferences.insert(reference);
		instantiation.finalGlobalAddressProvenance.emplace(reference, storage.decodeProvenance(provenance));
	}
	for (const auto &[name, value] : encoded.at("constants").items())
		instantiation.finalGlobalConstantValues.emplace(
			storage.decodeDefinition(Json::parse(name)), storage.decodeValue(value)
		);
	for (const Json &definition : encoded.at("taken"))
		instantiation.addressTakenGlobalReferences.insert(storage.decodeDefinition(definition));
	instantiation.externallyEscapedGlobalProvenance = storage.decodeProvenance(encoded.at("escaped"));
}
