// An inactive-body edit has already passed the stricter projection that masks
// only previously inactive functions. The all-function source projections mask
// at least those bodies, so their semantic digests remain valid. Refresh the
// changed body and match units without rebuilding those whole-program views.
std::optional<Json> refreshInactiveFunctionProjection(
	const ParseContext &context, const Json &observations, const std::map<std::string, Section *> &sections,
	IncrementalMatcherCache &matcherCache, const Json &prior, const Json &priorInactive, size_t &matchesRechecked,
	size_t &matchEntriesReused
) {
	Json selected = Json::object();
	std::map<const Section *, std::string> paths;
	std::set<std::string> reusable;
	for (const auto &[path, section] : sections) {
		paths.emplace(section, path);
		if (section->type == SectionType::Function && !section->patternDefinitions.empty() &&
			!containsNestedDefinition(*section))
			selected[path] = sectionIdentity(*section);
		if (reusableCodegenSection(*section))
			reusable.insert(path);
	}
	if (selected != prior.at("inactive") || reusable.size() != prior.at("matches").size())
		return std::nullopt;
	for (const std::string &path : reusable)
		if (!prior.at("matches").contains(path))
			return std::nullopt;
	Json bodies = definitionBodyDigests(context, sections);
	const Json &oldBodies = prior.at("bodies");
	if (bodies.size() != oldBodies.size())
		return std::nullopt;
	for (const auto &[path, _] : bodies.items())
		if (!oldBodies.contains(path))
			return std::nullopt;
	Json refreshed = prior;
	refreshed["scope_rule_bodies"] = scopeRuleBodyPaths(sections);
	refreshed["precedence_rules"] = precedenceRulesSignature(*context.mainSection);
	for (const auto &[path, body] : bodies.items()) {
		if (body == oldBodies.at(path))
			continue;
		if (!priorInactive.at("inactive").contains(path))
			return std::nullopt;
		refreshed["bodies"][path] = body;
		if (reusable.contains(path)) {
			ResolvedFunctionMatches result = resolvedFunctionMatches(
				sections.at(path), paths, matcherCache, &prior.at("match_entries").at(path), &prior.at("matches").at(path),
				&matchEntriesReused
			);
			refreshed["matches"][path] = std::move(result.fingerprint);
			refreshed["match_entries"][path] = std::move(result.entries);
			matchesRechecked++;
		}
	}
	for (const auto &[_, source] : context.importedFiles) {
		const std::string &uri = source->uri;
		const Json &content = observations.at(uri).at("content");
		for (const char *field : {"sources", "ambient_sources", "ambient_nonexecution_sources"}) {
			Json &entry = refreshed.at(field).at(uri);
			if (entry.at("content") != content) {
				entry["content"] = content;
				entry["mask"] = ""; // Force a new mask trace on the next executable edit.
			}
		}
	}
	return refreshed;
}

bool sameFrontendDiagnosticRecords(const Json &current, const Json &prior) {
	if (current.size() != prior.size())
		return false;
	auto sameMessage = [](const Json &left, const Json &right) {
		return left.at("configured") == right.at("configured") &&
			   (!left.at("configured").is_null() || left.at("text") == right.at("text"));
	};
	for (size_t index = 0; index < current.size(); index++) {
		const Json &left = current.at(index);
		const Json &right = prior.at(index);
		if (left.at("level") != right.at("level") || left.at("range") != right.at("range") ||
			!sameMessage(left.at("message"), right.at("message")))
			return false;
		const Json &leftRelated = left.at("related");
		const Json &rightRelated = right.at("related");
		if (leftRelated.size() != rightRelated.size())
			return false;
		for (size_t note = 0; note < leftRelated.size(); note++)
			if (leftRelated.at(note).at("range") != rightRelated.at(note).at("range") ||
				!sameMessage(leftRelated.at(note).at("message"), rightRelated.at(note).at("message")))
				return false;
	}
	return true;
}

bool canReuseInactiveBodies(
	const Json &previous, const Json &observations, const ParseContext &context,
	const std::map<std::string, Section *> &sections, const ResolvedDeclarationUnits &declarations,
	const Json &frontendDiagnostics, Json &currentProjection, SourceProjectionWork *projectionWork
) {
	if (!previous.contains("inactive_projection") || context.options.emitDebugInfo || context.options.traceExecution ||
		!context.scopeRuleWarnings.empty() || !compatibleObservations(previous, observations, context) ||
		!sameFrontendDiagnosticRecords(frontendDiagnostics, previous.at("frontend_diagnostic_records")))
		return false;
	std::set<std::string> diagnosticSources = previous.at("diagnostic_sources").get<std::set<std::string>>();
	for (const auto &[_, source] : context.importedFiles) {
		const std::string &uri = source->uri;
		if (diagnosticSources.contains(uri) && previous.at("files").at(uri).at("content") != observations.at(uri).at("content"))
			return false;
	}
	const Json &priorProjection = previous.at("inactive_projection");
	currentProjection = captureInactiveProjection(
		[&]() {
		return declarations.digest();
	}, context, observations, &priorProjection.at("inactive"), nullptr, &sections, &priorProjection.at("sources"),
		projectionWork, &priorProjection.at("sources")
	);
	return currentProjection.is_object() && currentProjection.at("declarations") == priorProjection.at("declarations");
}
