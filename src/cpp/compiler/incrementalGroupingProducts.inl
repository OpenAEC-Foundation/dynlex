Json encodeGroupingState(const GroupingState &state) {
	if (!state)
		return nullptr;
	Json nodes = Json::array();
	for (const auto &node : state->nodes)
		nodes.push_back(Json::array({node.arguments, node.explicitGroup}));
	return Json::array({state->root, std::move(nodes), state->ambiguityChecked});
}

GroupingState decodeGroupingState(const Json &encoded) {
	if (encoded.is_null())
		return {};
	auto state = std::make_shared<GroupingLayout>();
	state->root = encoded.at(0).get<size_t>();
	for (const Json &node : encoded.at(1))
		state->nodes.push_back({node.at(0).get<std::vector<size_t>>(), node.at(1).get<bool>()});
	state->ambiguityChecked = encoded.at(2).get<bool>();
	return state;
}

Json encodeGroupingLayouts(const std::shared_ptr<const GroupingProduct> &product, const InferenceSourceIndex &sources) {
	requireCompilerInvariant(product != nullptr, "inference query has no grouping product");
	Json result = Json::object();
	for (const auto &[line, transition] : product->groupings)
		result[sources.lineIdentity(line).dump()] =
			Json::array({encodeGroupingState(transition.input), encodeGroupingState(transition.output)});
	return result;
}

Json encodeGroupingProduct(const std::shared_ptr<const GroupingProduct> &product, const InferenceSourceIndex &sources) {
	auto instances = [&](const InferenceInstanceSet &scopes) -> std::optional<Json> {
		std::set<std::string> identities;
		for (const auto &scope : scopes) {
			auto identity = sources.parseContext().frozenStorageIdentities.find(scope.get());
			if (identity == sources.parseContext().frozenStorageIdentities.end())
				return std::nullopt;
			identities.insert(identity->second);
		}
		return identities;
	};
	auto required = instances(product->requiredInstances), provided = instances(product->providedInstances);
	if (!required || !provided)
		return nullptr;
	return Json{
		{"groupings", encodeGroupingLayouts(product, sources)},
		{"requires", std::move(*required)},
		{"provides", std::move(*provided)}
	};
}

std::shared_ptr<const GroupingProduct> decodeGroupingProduct(const Json &encoded, const InferenceSourceIndex &sources) {
	auto result = std::make_shared<GroupingProduct>();
	for (const auto &[identity, transition] : encoded.at("groupings").items())
		result->groupings.emplace(
			sources.decodeLine(Json::parse(identity)),
			GroupingTransition{decodeGroupingState(transition.at(0)), decodeGroupingState(transition.at(1))}
		);
	auto instances = [&](const Json &identities, InferenceInstanceSet &scopes) {
		ParseContext &context = sources.parseContext();
		for (const auto &identity : identities) {
			const std::string &key = identity.get_ref<const std::string &>();
			auto [logical, inserted] = context.logicalInstantiationScopes.try_emplace(key);
			if (inserted)
				logical->second = std::make_shared<VariableStorageScope>();
			context.frozenStorageIdentities[logical->second.get()] = key;
			scopes.insert(logical->second);
		}
	};
	instances(encoded.at("requires"), result->requiredInstances);
	instances(encoded.at("provides"), result->providedInstances);
	return result;
}

bool sameGroupingLayouts(const Json &currentGroups, const Json &priorGroups) {
	if (currentGroups.size() != priorGroups.size())
		return false;
	for (const auto &[line, transition] : currentGroups.items()) {
		if (!priorGroups.contains(line))
			return false;
		const Json &left = transition.at(1), &right = priorGroups.at(line).at(1);
		if (left.is_null() || right.is_null()) {
			if (left != right)
				return false;
		} else if (left.at(0) != right.at(0) || left.at(1) != right.at(1))
			return false;
	}
	return true;
}

bool sameGroupingOutputs(const Json &current, const Json &prior) {
	return current.at("provides") == prior.at("provides") &&
		   sameGroupingLayouts(current.at("groupings"), prior.at("groupings"));
}
