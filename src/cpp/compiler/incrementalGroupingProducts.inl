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

Json encodeGroupingProduct(const std::shared_ptr<const GroupingProduct> &product, const InferenceSourceIndex &sources) {
	requireCompilerInvariant(product != nullptr, "inference query has no grouping product");
	Json result = Json::object();
	for (const auto &[line, transition] : *product)
		result[sources.lineIdentity(line).dump()] =
			Json::array({encodeGroupingState(transition.input), encodeGroupingState(transition.output)});
	return result;
}

std::shared_ptr<const GroupingProduct> decodeGroupingProduct(const Json &encoded, const InferenceSourceIndex &sources) {
	auto result = std::make_shared<GroupingProduct>();
	for (const auto &[identity, transition] : encoded.items())
		result->emplace(
			sources.decodeLine(Json::parse(identity)),
			GroupingTransition{decodeGroupingState(transition.at(0)), decodeGroupingState(transition.at(1))}
		);
	return result;
}

bool sameGroupingOutputs(const Json &current, const Json &prior, bool includeCheckState = false) {
	if (current.size() != prior.size())
		return false;
	for (const auto &[line, transition] : current.items()) {
		if (!prior.contains(line))
			return false;
		const Json &left = transition.at(1), &right = prior.at(line).at(1);
		if (includeCheckState || left.is_null() || right.is_null()) {
			if (left != right)
				return false;
		} else if (left.at(0) != right.at(0) || left.at(1) != right.at(1))
			return false;
	}
	return true;
}
