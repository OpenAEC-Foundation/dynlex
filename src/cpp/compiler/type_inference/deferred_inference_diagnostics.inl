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
	size_t first;
	std::unordered_set<std::string> savedKeys;

  public:
	ScopedPreinferenceDiagnostics(ParseContext &context, Scope root)
		: context(context), first(context.diagnostics.size()), savedKeys(context.emittedDiagnosticKeys) {
		requireCompilerInvariant(!context.incrementalPreinferenceActive, "nested incremental preinference request");
		context.incrementalPreinferenceActive = true;
		context.preinferenceDiagnosticRoot = std::move(root);
		context.preinferencePublications.clear();
	}
	~ScopedPreinferenceDiagnostics() {
		context.diagnostics.resize(first);
		context.emittedDiagnosticKeys = std::move(savedKeys);
		context.preinferencePublications.clear();
		context.incrementalPreinferenceActive = false;
		context.preinferenceDiagnosticRoot.reset();
	}
};
