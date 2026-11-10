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

std::optional<Json> encodeAddressProvenance(const Section *section, const AddressProvenance &provenance) {
	std::set<std::string> targets;
	for (const VariableStorageKey &target : provenance.mayTargets) {
		if (target.scope || !target.definition)
			return std::nullopt;
		auto name = globalEffectName(section, target.definition);
		if (!name)
			return std::nullopt;
		targets.insert(*name);
	}
	return Json{
		{"targets", targets},
		{"null", provenance.mayBeNull},
		{"unknown", provenance.unknown},
		{"static", provenance.mayBeStatic}
	};
}

AddressProvenance decodeAddressProvenance(Section *section, const Json &encoded) {
	AddressProvenance result;
	for (const std::string &name : encoded.at("targets").get<std::set<std::string>>())
		result.mayTargets.insert({globalEffectReference(section, name), nullptr});
	result.mayBeNull = encoded.at("null").get<bool>();
	result.unknown = encoded.at("unknown").get<bool>();
	result.mayBeStatic = encoded.at("static").get<bool>();
	return result;
}

std::optional<Json> encodeGlobalConstant(const CompileTimeValue &value) {
	if (const auto *integer = std::get_if<std::int64_t>(&value))
		return Json::array({"signed", *integer});
	if (const auto *integer = std::get_if<std::uint64_t>(&value))
		return Json::array({"unsigned", *integer});
	if (const auto *number = std::get_if<double>(&value))
		return Json::array({"float_bits", std::bit_cast<std::uint64_t>(*number)});
	if (const auto *text = std::get_if<std::string>(&value))
		return Json::array({"text", *text});
	if (const auto *boolean = std::get_if<bool>(&value))
		return Json::array({"boolean", *boolean});
	if (const auto *pointer = std::get_if<CompileTimePointer>(&value))
		return Json::array({"pointer", pointer->address});
	return std::nullopt;
}

CompileTimeValue decodeGlobalConstant(const Json &encoded) {
	const std::string kind = encoded.at(0).get<std::string>();
	if (kind == "signed")
		return encoded.at(1).get<std::int64_t>();
	if (kind == "unsigned")
		return encoded.at(1).get<std::uint64_t>();
	if (kind == "float_bits")
		return std::bit_cast<double>(encoded.at(1).get<std::uint64_t>());
	if (kind == "text")
		return encoded.at(1).get<std::string>();
	if (kind == "boolean")
		return encoded.at(1).get<bool>();
	if (kind == "pointer")
		return CompileTimePointer{encoded.at(1).get<std::uint64_t>()};
	crashCompilerBug("cached global effect contains an unknown compile-time value kind");
}

std::optional<Json> encodeGlobalEffects(const Section *section, const Instantiation &instantiation) {
	Json writes = Json::object();
	for (VariableReference *reference : instantiation.writtenGlobalReferences) {
		auto name = globalEffectName(section, reference);
		if (!name)
			return std::nullopt;
		auto found = instantiation.finalGlobalAddressProvenance.find(reference);
		AddressProvenance provenance = found == instantiation.finalGlobalAddressProvenance.end()
										   ? AddressProvenance{.mayTargets = {}, .unknown = true}
										   : found->second;
		auto encoded = encodeAddressProvenance(section, provenance);
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
		auto encoded = encodeGlobalConstant(value);
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
	auto escaped = encodeAddressProvenance(section, instantiation.externallyEscapedGlobalProvenance);
	if (!escaped)
		return std::nullopt;
	return Json{{"writes", writes}, {"constants", constants}, {"taken", taken}, {"escaped", std::move(*escaped)}};
}

void decodeGlobalEffects(Section *section, const Json &encoded, Instantiation &instantiation) {
	for (const auto &[name, provenance] : encoded.at("writes").items()) {
		VariableReference *reference = globalEffectReference(section, name);
		instantiation.writtenGlobalReferences.insert(reference);
		instantiation.finalGlobalAddressProvenance.emplace(reference, decodeAddressProvenance(section, provenance));
	}
	for (const auto &[name, value] : encoded.at("constants").items())
		instantiation.finalGlobalConstantValues.emplace(globalEffectReference(section, name), decodeGlobalConstant(value));
	for (const std::string &name : encoded.at("taken").get<std::set<std::string>>())
		instantiation.addressTakenGlobalReferences.insert(globalEffectReference(section, name));
	instantiation.externallyEscapedGlobalProvenance = decodeAddressProvenance(section, encoded.at("escaped"));
}
