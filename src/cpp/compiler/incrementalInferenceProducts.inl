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
		: context(context), paths(paths), identities(paths), store(store), reads(reads),
		  sourceIdentities(context, paths, statistics), globalInputQueries(paths, sourceIdentities, statistics, reads) {
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
		if (!instance.inferenceReusedFromCache) {
			globalInputQueries.forgetQuery(instance.storageScope);
			if (auto found = queryScopes.find(instance.storageScope); found != queryScopes.end()) {
				const auto &[path, key] = found->second;
				if (retainedUnits.contains(path))
					retainedUnits[path].erase(key);
				if (retainedReferences.contains(path))
					retainedReferences[path].erase(key);
				if (!recorded.contains(path) || !recorded.at(path).contains(key))
					queryScopes.erase(found);
			}
		}
	}
	void discardedBatch(const std::function<void()> &capture) override {
		requireCompilerInvariant(!discardedClasses && !discardedStorage, "nested discarded inference snapshot");
		InferenceStorageIdentities storage(context, paths, identities, sourceIdentities);
		InferenceClassSnapshot snapshot(identities, [&](const Json &state) {
			store.recordClassSnapshotCapture();
			return store.saveClass(state);
		});
		discardedClasses = &snapshot;
		discardedStorage = &storage;
		struct Reset {
			InferenceClassSnapshot *&slot;
			InferenceStorageIdentities *&storage;
			~Reset() {
				slot = nullptr;
				storage = nullptr;
			}
		} reset{discardedClasses, discardedStorage};
		capture();
	}
	void retain(
		const Key &key, const Scope &scope, const Json &reference, const Json &proof, const Json *prior,
		const std::string &identity, const Json &selected
	) {
		retainedReferences[key.first][key.second] = reference;
		queryScopes[scope] = key;
		globalInputQueries.retainQuery(scope, proof);
		producers[identity] = {reference, selected, prior != nullptr};
		if (prior) {
			retainedUnits[key.first][key.second] = *prior;
			retainedUnits[key.first][key.second]["global_inputs"] = proof;
		}
	}
	void globalInput(const InferenceReadOwner &owner, VariableReference *reference) override {
		globalInputQueries.observe(owner, reference);
	}
	void conversionLookup(
		const InferenceReadOwner &owner, const lsp::SourceFile &source, bool implicit,
		const std::vector<PatternDefinition *> &definitions
	) override {
		Json interfaces = nullptr;
		if (context.hasCompleted(ParseContext::CompilationStage::InferredConstraints)) {
			interfaces = Json::object();
			for (const PatternDefinition *definition : definitions)
				interfaces[paths.at(definition->section)] = globalInputQueries.declaration(*definition->section);
		}
		reads.recordConversions(owner, source, implicit, definitions, std::move(interfaces));
	}
	void discarded(
		Section *section, const InstantiationKey &key, const Instantiation &instance, bool bodyRetained,
		const std::function<CompileTimeValue(const Expression *)> &values
	) override {
		if (instance.inferenceReusedFromCache && !instance.valueSummaryChanged)
			return;
		if (!context.hasCompleted(ParseContext::CompilationStage::InferredConstraints) ||
			!canPersistInferenceSummary(section, key, instance, identities))
			return;
		auto identity = inferenceSummaryKey(key, identities);
		if (!identity)
			return;
		requireCompilerInvariant(discardedStorage && discardedClasses, "discarded query has no immutable snapshot");
		InferenceStorageIdentities &storage = *discardedStorage;
		Json summary =
			captureSectionInferenceSummary(section, identities, storage, globalInputQueries, nullptr, nullptr, &*identity);
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
		if (instance.inferenceReusedFromCache)
			body = producers.at(instance.inferenceProductIdentity).reference.at("body");
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
		InferenceClassSnapshot &classSnapshot = *discardedClasses;
		if (body.is_null() && !instance.failure && instance.bodyInferred) {
			InferenceBodyWriter writer(identities, *sources, storage, classSnapshot, values);
			Json encodedBody = writer.capture(instance);
			body = store.saveBody(
				std::move(encodedBody), writer.valueReferenceCount(), writer.instantiationReferenceCount(),
				writer.captureMilliseconds()
			);
		}
		Json classes = captureInferenceFactClasses(record, identities, classSnapshot);
		recorded[path][*identity] = store.save(record, std::move(body), std::move(classes), std::move(*captured));
		queryInputProofs[path][*identity] = globalInputQueries.captureQuery(instance.storageScope);
		publications.emplace_back(path, *identity);
		queryScopes[instance.storageScope] = {path, *identity};
		if (instance.inferenceReusedFromCache)
			selectedVariants[path][*identity] = selection(instance.inferenceProductIdentity).get<std::set<std::string>>();
		else {
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
	const Json &selection(const std::string &identity) const { return producers.at(identity).selection; }
	bool previousProducer(const std::string &identity) const { return producers.at(identity).previous; }
	const InferenceSourceIndex &sourceIndex() const { return sourceIdentities; }
	InferenceGlobalInputs &globalInputs() const { return globalInputQueries; }
	const Json &inputProof(const Key &key) const { return queryInputProofs.at(key.first).at(key.second); }
	const auto &updates() const { return publications; }
	const Json &reference(const Key &key) const {
		return recorded.contains(key.first) && recorded.at(key.first).contains(key.second)
				   ? recorded.at(key.first).at(key.second)
				   : retainedReferences.at(key.first).at(key.second);
	}
	const Json *retainedUnit(const Key &key) const {
		return retainedUnits.contains(key.first) && retainedUnits.at(key.first).contains(key.second)
				   ? &retainedUnits.at(key.first).at(key.second)
				   : nullptr;
	}

  private:
	ParseContext &context;
	const std::map<const Section *, std::string> &paths;
	InferenceValueIdentities identities;
	InferenceSummaryStore &store;
	IncrementalInferenceReads &reads;
	InferenceSourceIndex sourceIdentities;
	mutable InferenceGlobalInputs globalInputQueries;
	std::unique_ptr<InferenceBodySources> sources;
	InferenceClassSnapshot *discardedClasses = nullptr;
	InferenceStorageIdentities *discardedStorage = nullptr;
	std::map<Scope, std::vector<Diagnostic>, std::owner_less<Scope>> pendingDiagnostics;
	Json recorded = Json::object();
	Json queryInputProofs = Json::object();
	Json retainedReferences = Json::object(), retainedUnits = Json::object();
	std::vector<Key> publications;
	Scopes queryScopes;
	struct Producer {
		Json reference;
		Json selection;
		bool previous;
	};
	std::map<std::string, Producer> producers;
	std::map<std::string, std::map<std::string, std::set<std::string>>> selectedVariants;
};
