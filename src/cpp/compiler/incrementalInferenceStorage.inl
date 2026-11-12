// Persistent storage identities describe declarations and logical invocations.
// Pointer addresses are meaningful only within the current compiler process.
#include "incrementalInferenceSourceIndex.inl"

class InferenceStorageIdentities {
  public:
	InferenceStorageIdentities(
		ParseContext &context, const std::map<const Section *, std::string> &paths, const InferenceValueIdentities &identities,
		const InferenceSourceIndex &sourceIndex
	)
		: context(context), paths(paths), identities(identities), sourceIndex(sourceIndex) {}
	std::optional<Json> encodeValue(const CompileTimeValue &value) const { return encodeInferenceValue(value, identities); }
	bool canDecodeValue(const Json &value) const { return decodeInferenceValue(value, identities).has_value(); }
	CompileTimeValue decodeValue(const Json &encoded) const {
		auto value = decodeInferenceValue(encoded, identities);
		requireCompilerInvariant(value.has_value(), "cached storage value has unrestored dependencies");
		return std::move(*value);
	}

	std::optional<Json> encodeProvenance(const AddressProvenance &provenance) {
		std::vector<Json> targets;
		for (const VariableStorageKey &target : provenance.mayTargets) {
			auto definition = encodeDefinition(target.definition);
			auto scope = encodeScope(target.scope);
			if (!definition || !scope)
				return std::nullopt;
			targets.push_back(Json::array({std::move(*definition), std::move(*scope)}));
		}
		std::ranges::sort(targets);
		return Json{
			{"targets", std::move(targets)},
			{"null", provenance.mayBeNull},
			{"unknown", provenance.unknown},
			{"static", provenance.mayBeStatic}
		};
	}

	bool canDecodeProvenance(const Json &encoded) const {
		return std::ranges::all_of(encoded.at("targets"), [&](const Json &target) {
			return canDecodeScope(target.at(1));
		});
	}

	AddressProvenance decodeProvenance(const Json &encoded) {
		AddressProvenance result;
		for (const Json &target : encoded.at("targets")) {
			result.mayTargets.insert({decodeDefinition(target.at(0)), decodeScope(target.at(1))});
		}
		result.mayBeNull = encoded.at("null").get<bool>();
		result.unknown = encoded.at("unknown").get<bool>();
		result.mayBeStatic = encoded.at("static").get<bool>();
		return result;
	}

	// Executable-body records share the same declaration and invocation keys.
	VariableReference *decodeDefinition(const Json &definition) const {
		Section *section = identities.sections.at(definition.at(0).get<std::string>());
		return normalizeBindingReference(section->variableDefinitions.at(definition.at(1).get<std::string>()));
	}
	std::optional<Json> encodeDefinition(VariableReference *reference) {
		if (!reference)
			return std::nullopt;
		return sourceIndex.definition(reference);
	}

	void indexScopeOrigins() {
		for (const auto &[key, scope] : context.instantiationStorageScopes)
			instantiations.emplace(scope.get(), &key);
		for (const auto &[key, scope] : context.invocationStorageScopes)
			invocations.emplace(scope.get(), &key);
		indexedScopes = true;
	}
	const std::pair<Section *, InstantiationKey> *instantiationOrigin(const VariableStorageScope *scope) {
		if (!indexedScopes)
			indexScopeOrigins();
		auto found = instantiations.find(scope);
		return found == instantiations.end() ? nullptr : found->second;
	}
	const std::tuple<std::shared_ptr<const VariableStorageScope>, const Expression *, size_t> *
	invocationOrigin(const VariableStorageScope *scope) {
		if (!indexedScopes)
			indexScopeOrigins();
		auto found = invocations.find(scope);
		return found == invocations.end() ? nullptr : found->second;
	}

	std::optional<Json> encodeScope(const std::shared_ptr<const VariableStorageScope> &scope) {
		if (!scope)
			return Json(nullptr);
		if (auto frozen = context.frozenStorageIdentities.find(scope.get()); frozen != context.frozenStorageIdentities.end())
			return Json::parse(frozen->second);
		if (!indexedScopes)
			indexScopeOrigins();
		if (auto found = encodedScopes.find(scope.get()); found != encodedScopes.end())
			return found->second;
		std::optional<Json> result;
		if (auto found = instantiations.find(scope.get()); found != instantiations.end()) {
			const auto &[section, key] = *found->second;
			result = encodeInferenceKey(key, identities);
			if (result)
				(*result)["owner"] = paths.at(section);
		} else if (auto found = invocations.find(scope.get()); found != invocations.end()) {
			const auto &[parent, expression, iteration] = *found->second;
			auto parentIdentity = encodeScope(parent);
			auto site = encodeExpression(expression);
			if (parentIdentity && site)
				result = Json{{"parent", std::move(*parentIdentity)}, {"site", std::move(*site)}, {"iteration", iteration}};
		}
		encodedScopes.emplace(scope.get(), result);
		return result;
	}

	bool canDecodeScope(const Json &scope) const {
		if (scope.is_null())
			return true;
		if (scope.contains("parent"))
			return canDecodeScope(scope.at("parent"));
		return decodeInferenceKey(scope, identities).has_value();
	}

	std::shared_ptr<const VariableStorageScope> decodeScope(const Json &scope) {
		if (scope.is_null())
			return {};
		if (scope.contains("parent")) {
			const Json &site = scope.at("site");
			Expression *expression = sourceIndex.decodeExpression(site);
			return context.storageScopeForInvocation(
				expression, scope.at("iteration").get<size_t>(), decodeScope(scope.at("parent"))
			);
		}
		Section *section = identities.sections.at(scope.at("owner").get<std::string>());
		auto key = decodeInferenceKey(scope, identities);
		requireCompilerInvariant(key.has_value(), "cached invocation has unrestored arguments");
		return context.storageScopeForInstantiation(section, *key);
	}

	// Keep source-template edges attached to this process's canonical nodes.
	std::optional<Json> encodeExpression(const Expression *expression) const { return sourceIndex.expression(expression); }
	Expression *decodeExpression(const Json &site) const { return sourceIndex.decodeExpression(site); }

  private:
	ParseContext &context;
	const std::map<const Section *, std::string> &paths;
	const InferenceValueIdentities &identities;
	const InferenceSourceIndex &sourceIndex;
	std::unordered_map<const VariableStorageScope *, const std::pair<Section *, InstantiationKey> *> instantiations;
	std::unordered_map<
		const VariableStorageScope *,
		const std::tuple<std::shared_ptr<const VariableStorageScope>, const Expression *, size_t> *>
		invocations;
	std::unordered_map<const VariableStorageScope *, std::optional<Json>> encodedScopes;
	bool indexedScopes = false;
};
