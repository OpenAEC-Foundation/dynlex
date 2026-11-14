// Unit products contain relative inference traces, portable source anchors and
// configured messages. Publication identities are shared across enclosing units.
Json inferenceDiagnosticText(std::string_view text, const std::shared_ptr<const ConfiguredDiagnosticMessage> &configured) {
	Json result = cachedDiagnosticText(text, configured);
	if (configured)
		result["text"] = "";
	return result;
}

Json captureUnitDiagnostic(const Diagnostic &diagnostic, InferenceBodySources &sources) {
	Json related = Json::array(), fixes = Json::array();
	for (const RelatedInfo &note : diagnostic.relatedInfo)
		related.push_back(
			Json::array(
				{inferenceDiagnosticText(note.message, note.configuredMessage), sources.range(note.range), note.inferenceDepth}
			)
		);
	for (const DiagnosticFix &fix : diagnostic.quickFixes)
		fixes.push_back(Json::array({fix.title, sources.range(fix.range), fix.replacement}));
	return Json{
		{"level", static_cast<int>(diagnostic.level)},
		{"message", inferenceDiagnosticText(diagnostic.message, diagnostic.configuredMessage)},
		{"range", sources.range(diagnostic.range)},
		{"related", std::move(related)},
		{"fixes", std::move(fixes)},
		{"discriminator", diagnostic.emission ? diagnostic.emission->discriminator : ""},
		{"publication",
		 diagnostic.emission && diagnostic.emission->publication ? diagnostic.emission->publication->identity : ""}
	};
}

Diagnostic restoreUnitDiagnostic(
	ParseContext &context, const Json &record, InferenceBodySources &sources,
	const std::shared_ptr<const VariableStorageScope> &scope = {}
) {
	auto configured = [](const Json &text) -> std::shared_ptr<const ConfiguredDiagnosticMessage> {
		return text.at("configured").is_null()
				   ? nullptr
				   : std::make_shared<const ConfiguredDiagnosticMessage>(restoreConfiguredMessage(text.at("configured")));
	};
	Diagnostic diagnostic;
	diagnostic.level = static_cast<Diagnostic::Level>(record.at("level").get<int>());
	diagnostic.message = renderCachedDiagnosticText(context, record.at("message"));
	diagnostic.configuredMessage = configured(record.at("message"));
	diagnostic.range = sources.range(record.at("range"));
	for (const Json &note : record.at("related")) {
		int depth = note.at(2).get<int>();
		diagnostic.relatedInfo.push_back(
			{renderCachedDiagnosticText(context, note.at(0)), sources.range(note.at(1)), configured(note.at(0)),
			 depth >= 0 ? scope : nullptr, depth, depth >= 0 ? 0 : -1}
		);
	}
	for (const Json &fix : record.at("fixes"))
		diagnostic.quickFixes.push_back({fix.at(0).get<std::string>(), sources.range(fix.at(1)), fix.at(2).get<std::string>()});
	const auto &discriminator = record.at("discriminator").get_ref<const std::string &>();
	const auto &identity = record.at("publication").get_ref<const std::string &>();
	if (!discriminator.empty() || !identity.empty()) {
		auto &emission = diagnostic.editEmission();
		emission.discriminator = discriminator;
		if (!identity.empty()) {
			auto [publication, inserted] = context.inferenceDiagnosticPublications.try_emplace(identity);
			if (inserted)
				publication->second = std::make_shared<DiagnosticPublication>(DiagnosticPublication{false, identity});
			emission.publication = publication->second;
		}
	}
	return diagnostic;
}

std::optional<Json> captureInferenceDiagnosticRecords(
	const std::vector<Diagnostic> &diagnostics, const std::function<InferenceBodySources &()> &sources,
	InferenceStorageIdentities &storage
) {
	Json records = Json::array();
	for (const Diagnostic &diagnostic : diagnostics) {
		const auto &emission = *diagnostic.emission;
		Json message = inferenceDiagnosticText(diagnostic.message, diagnostic.configuredMessage);
		Json range = sources().range(diagnostic.range);
		if (emission.publication->identity.empty()) {
			auto origin = storage.encodeScope(emission.owner);
			if (!origin)
				return std::nullopt; // This unit requires an encodable logical emission origin.
			emission.publication->identity = digest(
				Json::array({std::move(*origin), range, static_cast<int>(diagnostic.level), message, emission.discriminator})
					.dump()
			);
		}
		Json record = captureUnitDiagnostic(diagnostic, sources());
		record["traced"] = emission.traceBase >= 0;
		records.push_back(std::move(record));
	}
	return records;
}

std::optional<Json> captureInferenceDiagnostics(
	const ParseContext &context, const std::shared_ptr<const VariableStorageScope> &scope,
	const std::function<InferenceBodySources &()> &sources, InferenceStorageIdentities &storage
) {
	auto found = context.deferredInferenceDiagnostics.find(scope);
	return found == context.deferredInferenceDiagnostics.end()
			   ? std::optional<Json>{Json::array()}
			   : captureInferenceDiagnosticRecords(found->second, sources, storage);
}

void restoreInferenceDiagnostics(
	ParseContext &context, const std::shared_ptr<const VariableStorageScope> &scope, const Json &records,
	const std::function<InferenceBodySources &()> &sources
) {
	// Logical scope identities survive rejected trials. Decode each immutable
	// cached product once even when its fact slot is restored again.
	if (records.empty() || context.deferredInferenceDiagnostics.contains(scope))
		return;
	auto &restored = context.deferredInferenceDiagnostics[scope];
	for (const Json &record : records) {
		Diagnostic diagnostic = restoreUnitDiagnostic(context, record, sources(), scope);
		auto &emission = diagnostic.editEmission();
		emission.owner = scope;
		emission.traceBase = record.at("traced").get<bool>() ? 0 : -1;
		restored.push_back(std::move(diagnostic));
	}
	context.inferenceDiagnosticUnitsRestored++;
	context.inferenceDiagnosticRecordsRestored += records.size();
}

Json captureRootInferenceDiagnostics(
	ParseContext &context, const std::map<const Section *, std::string> &paths, const InferenceSourceIndex &sourceIndex
) {
	auto root = context.deferredInferenceDiagnostics.find({});
	if (root == context.deferredInferenceDiagnostics.end() || root->second.empty())
		return Json::array();
	InferenceValueIdentities identities(paths);
	InferenceStorageIdentities storage(context, paths, identities, sourceIndex);
	InferenceBodySources sources(context, identities);
	auto records = captureInferenceDiagnostics(context, {}, [&]() -> auto & {
		return sources;
	}, storage);
	return records ? std::move(*records) : Json(nullptr);
}
