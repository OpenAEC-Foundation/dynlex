bool IncrementalClassification::canReuse(const Unit &unit) {
	if (!prior || unit.environment || !unit.old || context.options.emitDebugInfo || context.options.traceExecution ||
		unit.section->parent != context.mainSection || !reusableResolutionSection(*unit.section) || unit.references.empty() ||
		!sameBody(unit.path))
		return false;
	for (const Json &source : previous->at("diagnostic_sources"))
		if (source == resolutionSourceFile(*unit.section).uri)
			return false;
	const Json &projection = previous->at("all_function_projection");
	const Json &matcher = projection.at("matcher_projection");
	if (!matcher.at("body_units").contains(unit.path) ||
		!cache.contains(matcher.at("body_units").at(unit.path).get_ref<const std::string &>()))
		return false;
	auto references = savedReferenceKeys(projection, cache, unit.path);
	if (!references || references->size() != unit.references.size())
		return false;
	for (const auto &reference : *references)
		for (std::string_view key : {reference.initialKey, reference.finalKey})
			if (!key.empty() && !cache.contains(key))
				return false;
	return true;
}

std::string IncrementalClassification::exported(const Unit &unit) const {
	if (unit.old && (unit.selected || !dirty.contains(unit.path)))
		return unit.old->at(Export).get<std::string>();
	return digest(unit.environment ? globalResolutionIdentity(*unit.section).dump() : declarationInput(unit));
}

void IncrementalClassification::preparePropagation(
	std::vector<std::vector<size_t>> reverse, const std::vector<bool> &inputsChanged
) {
	reverseEdges = std::move(reverse);
	const size_t count = reverseEdges.size();
	std::vector<std::vector<size_t>> forward(count);
	for (size_t target = 0; target < count; target++)
		for (size_t reader : reverseEdges[target])
			forward[reader].push_back(target);

	// A visible-name environment publishes only after all candidate providers
	// have settled. Evaluate its dirty provider closure before that boundary.
	std::vector<bool> eager = inputsChanged;
	std::stack<size_t> pending;
	for (size_t index = 0; index < units.size(); index++)
		if (units[index].environment && dirty.contains(units[index].path)) {
			eager[index] = true;
			pending.push(index);
		}
	std::vector<bool> providersVisited(count);
	while (!pending.empty()) {
		size_t reader = pending.top();
		pending.pop();
		if (providersVisited[reader])
			continue;
		providersVisited[reader] = true;
		for (size_t target : forward[reader]) {
			eager[target] = true;
			pending.push(target);
		}
	}

	// Kosaraju's two traversals use explicit stacks, including for large cycles.
	std::vector<bool> visited(count);
	std::vector<size_t> order;
	for (size_t root = 0; root < count; root++) {
		if (visited[root])
			continue;
		std::stack<std::pair<size_t, size_t>> traversal;
		visited[root] = true;
		traversal.emplace(root, 0);
		while (!traversal.empty()) {
			auto &[node, next] = traversal.top();
			if (next == forward[node].size()) {
				order.push_back(node);
				traversal.pop();
				continue;
			}
			size_t target = forward[node][next++];
			if (!visited[target]) {
				visited[target] = true;
				traversal.emplace(target, 0);
			}
		}
	}
	componentOf.assign(count, count);
	for (size_t root : std::views::reverse(order)) {
		if (componentOf[root] != count)
			continue;
		size_t component = components.size();
		components.emplace_back();
		componentOf[root] = component;
		pending.push(root);
		while (!pending.empty()) {
			size_t node = pending.top();
			pending.pop();
			components[component].push_back(node);
			for (size_t reader : reverseEdges[node])
				if (componentOf[reader] == count) {
					componentOf[reader] = component;
					pending.push(reader);
				}
		}
	}
	eligible.resize(units.size());
	observedExports.resize(units.size());
	for (size_t index = 0; index < units.size(); index++) {
		const Unit &unit = units[index];
		eligible[index] = canReuse(unit);
		if (!unit.environment && unit.hasCandidates && !eligible[index])
			eager[index] = true;
		if (unit.old)
			observedExports[index] = unit.old->at(Export).get<std::string>();
	}
	evaluate.assign(units.size(), false);
	for (const auto &component : components)
		if (std::ranges::any_of(component, [&](size_t index) {
			return eager[index];
		}))
			for (size_t index : component)
				if (index < units.size()) {
					evaluate[index] = true;
					work.unitsInvalidated++;
				}
}

std::vector<Section *> IncrementalClassification::invalidated() {
	Timer timer{work};
	std::vector<Section *> result;
	if (!prior || captured || !validationNeeded)
		return result;
	std::vector<size_t> changed;
	for (size_t index = 0; index < units.size(); index++) {
		if (!evaluate[index] || !dirty.contains(units[index].path))
			continue;
		std::string current = exported(units[index]);
		if (current != observedExports[index]) {
			observedExports[index] = std::move(current);
			changed.push_back(index);
		}
	}
	if (!removedExportsObserved) {
		for (size_t index = units.size(); index < reverseEdges.size(); index++)
			changed.push_back(index);
		removedExportsObserved = true;
	}
	for (size_t target : changed)
		for (size_t reader : reverseEdges[target])
			for (size_t index : components[componentOf[reader]]) {
				if (index >= units.size() || !units[index].selected)
					continue;
				Unit &unit = units[index];
				unit.selected = false;
				work.unitsReused--;
				work.parametersReused -= data(unit.outputs).size();
				work.unitsReactivated++;
				if (!evaluate[index]) {
					evaluate[index] = true;
					work.unitsInvalidated++;
				}
				result.push_back(unit.section);
			}
	return result;
}
