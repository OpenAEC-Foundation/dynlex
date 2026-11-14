// The execution source projection already proves the same top-level
// statements. A body edit can shift their physical line numbers.
bool sameRootGlobalBindings(const Json &prior, const Json &current) {
	for (size_t group = 0; group < 2; group++) {
		const Json &before = prior.at(group);
		const Json &after = current.at(group);
		if (before.size() != after.size())
			return false;
		for (const auto &[name, location] : before.items()) {
			auto binding = after.find(name);
			if (binding == after.end() || location.at(0) != binding->at(0) || location.at(2) != binding->at(2))
				return false;
		}
	}
	return prior.at(2) == current.at(2);
}
