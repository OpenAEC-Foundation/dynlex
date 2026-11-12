// Persistent storage identities describe declarations and logical invocations.
// Pointer addresses are meaningful only within the current compiler process.
class InferenceStorageIdentities {
  public:
	InferenceStorageIdentities(
		ParseContext &context, const std::map<const Section *, std::string> &paths, const InferenceValueIdentities &identities
	)
		: context(context), paths(paths), identities(identities) {
		for (const auto &[section, path] : paths)
			sections.emplace(path, const_cast<Section *>(section));
	}
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
			const Json &definition = target.at(0);
			Section *section = sections.at(definition.at(0).get<std::string>());
			VariableReference *reference = section->variableDefinitions.at(definition.at(1).get<std::string>());
			result.mayTargets.insert({normalizeBindingReference(reference), decodeScope(target.at(1))});
		}
		result.mayBeNull = encoded.at("null").get<bool>();
		result.unknown = encoded.at("unknown").get<bool>();
		result.mayBeStatic = encoded.at("static").get<bool>();
		return result;
	}

  private:
	std::optional<Json> encodeDefinition(VariableReference *reference) {
		if (!reference)
			return std::nullopt;
		if (!indexedDefinitions) {
			for (const auto &[path, section] : sections)
				for (const auto &[name, definition] : section->variableDefinitions)
					definitions.emplace(normalizeBindingReference(definition), Json::array({path, name}));
			indexedDefinitions = true;
		}
		auto found = definitions.find(reference);
		requireCompilerInvariant(found != definitions.end(), "storage origin has no lexical declaration");
		return found->second;
	}

	void indexScopeOrigins() {
		for (const auto &[key, scope] : context.instantiationStorageScopes)
			instantiations.emplace(scope.get(), &key);
		for (const auto &[key, scope] : context.invocationStorageScopes)
			invocations.emplace(scope.get(), &key);
		indexedScopes = true;
	}

	std::optional<Json> encodeScope(const std::shared_ptr<const VariableStorageScope> &scope) {
		if (!scope)
			return Json(nullptr);
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
			Section *section = sections.at(site.at(0).get<std::string>());
			Expression *expression = sourceExpression(*section, site.at(1).get<std::string>(), site.at(2).get<size_t>());
			for (const Json &component : site.at(3))
				expression =
					component.is_null() ? expression->dataDeclaration : expression->arguments.at(component.get<size_t>());
			return context.storageScopeForInvocation(
				expression, scope.at("iteration").get<size_t>(), decodeScope(scope.at("parent"))
			);
		}
		Section *section = sections.at(scope.at("owner").get<std::string>());
		auto key = decodeInferenceKey(scope, identities);
		requireCompilerInvariant(key.has_value(), "cached invocation has unrestored arguments");
		return context.storageScopeForInstantiation(section, *key);
	}

	static Expression *sourceExpression(const Section &section, const std::string &text, size_t occurrence) {
		for (const CodeLine *line : section.codeLines)
			if (line->expression && digest(line->rightTrimmedText) == text && occurrence-- == 0)
				return line->expression;
		crashCompilerBug("cached storage invocation lost its source expression");
	}

	static void
	indexExpression(const Expression *expression, const Json &site, std::unordered_map<const Expression *, Json> &index) {
		if (!expression || !index.emplace(expression, site).second)
			return;
		auto child = [&](const Expression *value, Json component) {
			Json nested = site;
			nested.at(3).push_back(std::move(component));
			indexExpression(value, nested, index);
		};
		for (size_t argument = 0; argument < expression->arguments.size(); argument++)
			child(expression->arguments[argument], argument);
		child(expression->dataDeclaration, nullptr);
	}

	std::optional<Json> encodeExpression(const Expression *expression) {
		if (!indexedExpressions) {
			for (const auto &[path, section] : sections) {
				std::unordered_map<std::string, size_t> occurrences;
				for (const CodeLine *line : section->codeLines)
					if (line->expression) {
						std::string text = digest(line->rightTrimmedText);
						indexExpression(
							line->expression, Json::array({path, text, occurrences[text]++, Json::array()}), expressions
						);
					}
			}
			indexedExpressions = true;
		}
		auto found = expressions.find(expression);
		return found == expressions.end() ? std::nullopt : std::optional<Json>{found->second};
	}

	ParseContext &context;
	const std::map<const Section *, std::string> &paths;
	const InferenceValueIdentities &identities;
	std::map<std::string, Section *> sections;
	std::unordered_map<const VariableStorageScope *, const std::pair<Section *, InstantiationKey> *> instantiations;
	std::unordered_map<
		const VariableStorageScope *,
		const std::tuple<std::shared_ptr<const VariableStorageScope>, const Expression *, size_t> *>
		invocations;
	std::unordered_map<const VariableStorageScope *, std::optional<Json>> encodedScopes;
	std::unordered_map<const VariableReference *, Json> definitions;
	std::unordered_map<const Expression *, Json> expressions;
	bool indexedScopes = false;
	bool indexedExpressions = false;
	bool indexedDefinitions = false;
};
