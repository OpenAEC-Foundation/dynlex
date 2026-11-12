// Speculative inference produces diagnostics relative to its concrete unit.
// Actual calls supply the outer trace and publish shared emissions once.
static void publishDeferredInferenceDiagnostics(InferenceContext &context, const Instantiation &instance) {
	if (context.parseContext.deferredInferenceDiagnostics.empty() || (context.trial && !context.pendingDiagnostics) ||
		context.suppressDiagnostics)
		return;
	requireCompilerInvariant(instance.body != nullptr, "deferred diagnostic publication requires its concrete body");
	auto found = context.parseContext.deferredInferenceDiagnostics.find(instance.body->storageScope);
	if (found == context.parseContext.deferredInferenceDiagnostics.end())
		return;
	for (const Diagnostic &stored : found->second) {
		requireCompilerInvariant(stored.emission && stored.emission->publication, "deferred diagnostic lost its publication");
		Diagnostic diagnostic = stored;
		auto &emission = diagnostic.editEmission();
		int base = static_cast<int>(context.diagnosticTraceDepth());
		for (RelatedInfo &note : diagnostic.relatedInfo)
			if (note.inferenceDepth >= 0) {
				note.inferenceDepth += base;
				note.inferenceOwnerBase += base;
			}
		if (emission.traceBase >= 0)
			emission.traceBase += base;
		context.appendCurrentInferenceTrace(diagnostic);
		context.queueDiagnostic(std::move(diagnostic));
	}
}

class ScopedPreinferenceDiagnostics {
	using Scope = std::shared_ptr<const VariableStorageScope>;
	ParseContext &context;
	Scope root;
	size_t first;
	std::unordered_set<std::string> savedKeys;
	bool succeeded = false;

  public:
	ScopedPreinferenceDiagnostics(ParseContext &context, Scope root)
		: context(context), root(std::move(root)), first(context.diagnostics.size()), savedKeys(context.emittedDiagnosticKeys) {
		requireCompilerInvariant(!context.incrementalPreinferenceActive, "nested incremental preinference request");
		context.incrementalPreinferenceActive = true;
		context.preinferencePublications.clear();
	}
	void accept() { succeeded = true; }
	~ScopedPreinferenceDiagnostics() {
		for (size_t index = first; index < context.diagnostics.size(); index++) {
			Diagnostic diagnostic = context.diagnostics[index];
			if (!succeeded && diagnostic.level == Diagnostic::Level::Error)
				continue;
			auto &emission = diagnostic.editEmission();
			if (!emission.publication)
				emission.publication = std::make_shared<DiagnosticPublication>();
			std::map<Scope, int, std::owner_less<Scope>> owners{{root, 0}};
			if (emission.owner)
				owners[emission.owner] = emission.traceBase;
			for (const RelatedInfo &note : diagnostic.relatedInfo)
				if (note.inferenceOwner)
					owners[note.inferenceOwner] = note.inferenceOwnerBase;
			for (const auto &[owner, base] : owners) {
				Diagnostic relative = diagnostic;
				auto &relativeEmission = relative.editEmission();
				auto &pending = context.deferredInferenceDiagnostics[owner];
				if (std::ranges::any_of(pending, [&](const Diagnostic &prior) {
					return prior.emission->publication == relativeEmission.publication;
				}))
					continue;
				std::erase_if(relative.relatedInfo, [&](const RelatedInfo &note) {
					return note.inferenceDepth >= 0 && note.inferenceDepth < base;
				});
				for (RelatedInfo &note : relative.relatedInfo)
					if (note.inferenceDepth >= 0) {
						note.inferenceDepth -= base;
						note.inferenceOwnerBase -= base;
					}
				if (relativeEmission.traceBase >= 0)
					relativeEmission.traceBase -= base;
				pending.push_back(std::move(relative));
				context.preinferredDiagnosticsDeferred++;
			}
		}
		context.diagnostics.resize(first);
		context.emittedDiagnosticKeys = std::move(savedKeys);
		context.preinferencePublications.clear();
		context.incrementalPreinferenceActive = false;
	}
};
