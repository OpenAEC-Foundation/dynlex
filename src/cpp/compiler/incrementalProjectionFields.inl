struct ProjectionCopyWork {
	size_t copied = 0, skipped = 0;
};

// Each caller supplies products it will publish anew. Keep all other stage
// products, without constructing and destroying obsolete copies of those fields.
Json copyProjectionFields(const Json &prior, std::initializer_list<std::string_view> replaced, ProjectionCopyWork &work) {
	Json result = Json::object();
	for (const auto &[field, value] : prior.items()) {
		if (std::find(replaced.begin(), replaced.end(), field) != replaced.end()) {
			work.skipped++;
			continue;
		}
		result[field] = value;
		work.copied++;
	}
	return result;
}
