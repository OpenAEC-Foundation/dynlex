// Freeze completed semantic queries before a rejected trial destroys their
// instantiation slots and class-layout state. Products have portable keys;
// execution state continues to obey ordinary trial rollback.
class IncrementalInferenceProducts : public InferenceProductHooks {
  public:
	using Key = std::pair<std::string, std::string>;
	using Scope = std::shared_ptr<const VariableStorageScope>;
	using Scopes = std::map<Scope, Key, std::owner_less<Scope>>;
	IncrementalInferenceProducts(
		ParseContext &context, const std::map<const Section *, std::string> &paths, InferenceSummaryStore &store,
		IncrementalInferenceReads &reads, BuildStatistics &statistics
	)
		: context(context), paths(paths), identities(paths), store(store), reads(reads), sourceIdentities(paths, statistics) {
		context.inferenceProducts = this;
	}
	~IncrementalInferenceProducts() { context.inferenceProducts = nullptr; }
	std::optional<std::string> storageIdentity(Section *section, const InstantiationKey &key) const override {
		auto encoded = encodeInferenceKey(key, identities);
		if (!encoded)
			return {};
		(*encoded)["owner"] = paths.at(section);
		return encoded->dump();
	}
	void completed(const Instantiation &instance, std::vector<Diagnostic> diagnostics) override {
		pendingDiagnostics[instance.storageScope] = std::move(diagnostics);
	}
	void conversionLookup(
		const InferenceReadOwner &owner, const lsp::SourceFile &source, bool implicit,
		const std::vector<PatternDefinition *> &definitions
	) override {
		reads.recordConversions(owner, source, implicit, definitions);
	}
	void discarded(
		Section *section, const InstantiationKey &key, const Instantiation &instance, bool bodyRetained,
		const std::function<CompileTimeValue(const Expression *)> &values
	) override {
		if (!context.hasCompleted(ParseContext::CompilationStage::InferredConstraints) ||
			!canPersistInferenceSummary(section, key, instance, identities))
			return;
		auto identity = inferenceSummaryKey(key, identities);
		if (!identity)
			return;
		Json globals = captureGlobalTypeProjection(paths).value_or(Json(nullptr));
		InferenceStorageIdentities storage(context, paths, identities, sourceIdentities);
		Json summary = captureSectionInferenceSummary(section, identities, storage, globals, nullptr, nullptr, &*identity);
		if (!summary.contains(*identity))
			return;
		if (!sources)
			sources = std::make_unique<InferenceBodySources>(context, identities);
		auto sourceRecords = [&]() -> auto & {
			return *sources;
		};
		Json &record = summary.at(*identity);
		if (instance.failure)
			record["failure"] = captureInferenceFailure(*instance.failure, *sources);
		const auto &path = paths.at(section);
		Json body = nullptr;
		if (recorded.contains(path) && recorded.at(path).contains(*identity)) {
			const Json &previous = recorded.at(path).at(*identity);
			const Json *retained = store.load(previous);
			requireCompilerInvariant(retained, "newly captured inference observations disappeared");
			mergeInferenceObservedValues(record, *retained);
			if (bodyRetained)
				body = previous.at("body");
		}
		std::vector<Diagnostic> diagnostics = pendingDiagnostics[instance.storageScope];
		if (auto prior = context.deferredInferenceDiagnostics.find(instance.storageScope);
			prior != context.deferredInferenceDiagnostics.end())
			for (const auto &diagnostic : prior->second)
				if (std::ranges::none_of(diagnostics, [&](const auto &other) {
					return other.sameEmission(diagnostic);
				}))
					diagnostics.push_back(diagnostic);
		auto captured = captureInferenceDiagnosticRecords(diagnostics, sourceRecords, storage);
		if (!captured)
			return;
		InferenceClassSnapshot classSnapshot(identities);
		if (!bodyRetained && !instance.failure && instance.bodyInferred) {
			InferenceBodyWriter writer(identities, *sources, storage, classSnapshot, values);
			body = store.saveBody(writer.capture(instance));
		}
		Json classes = store.saveClasses(captureInferenceFactClasses(record, identities, classSnapshot));
		recorded[path][*identity] = store.save(record, std::move(body), std::move(classes), std::move(*captured));
		queryScopes[instance.storageScope] = {path, *identity};
		if (!instance.inferenceReusedFromCache) {
			std::vector<Section *> inferred;
			collectInferredSectionDependencies(instance.body.get(), inferred);
			auto &selected = selectedVariants[path][*identity];
			for (const Section *dependency : inferred)
				selected.insert(paths.at(dependency));
		}
	}
	const Json &references() const { return recorded; }
	const Scopes &scopes() const { return queryScopes; }
	const auto &variants() const { return selectedVariants; }
	const InferenceSourceIndex &sourceIndex() const { return sourceIdentities; }

  private:
	ParseContext &context;
	const std::map<const Section *, std::string> &paths;
	InferenceValueIdentities identities;
	InferenceSummaryStore &store;
	IncrementalInferenceReads &reads;
	InferenceSourceIndex sourceIdentities;
	std::unique_ptr<InferenceBodySources> sources;
	std::map<Scope, std::vector<Diagnostic>, std::owner_less<Scope>> pendingDiagnostics;
	Json recorded = Json::object();
	Scopes queryScopes;
	std::map<std::string, std::map<std::string, std::set<std::string>>> selectedVariants;
};
