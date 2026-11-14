#include "inferenceProductHooks.h"

static void noteInferenceProduct(Instantiation &instance, const InferenceContext &context) {
	if (!context.parseContext.inferenceProducts || !instance.storageScope)
		return;
	std::vector<Diagnostic> diagnostics;
	if (context.pendingDiagnostics)
		for (const Diagnostic &diagnostic : *context.pendingDiagnostics) {
			if (!diagnostic.emission || diagnostic.level == Diagnostic::Level::Error)
				continue;
			const auto &emission = *diagnostic.emission;
			auto owner = std::ranges::find(emission.retentionScopes, instance.storageScope, [](const auto &entry) {
				return entry.first;
			});
			if (emission.owner == instance.storageScope)
				diagnostics.push_back(diagnostic.relativeToInference(emission.traceBase));
			else if (owner != emission.retentionScopes.end())
				diagnostics.push_back(diagnostic.relativeToInference(owner->second));
		}
	context.parseContext.inferenceProducts->completed(instance, std::move(diagnostics));
}

static std::shared_ptr<const InferenceFailure> captureInferenceFailure(const InferenceContext &context, int base) {
	// Preinference precedes entry execution and can observe globals before their
	// types exist. Only an actual demand establishes a terminal error product.
	if (context.parseContext.incrementalPreinferenceActive)
		return {};
	if (!context.hasTypeFailureDiagnostic && context.typeFailureDetail.empty())
		return {}; // A separately emitted diagnostic has no pending failure to replay.
	auto failure = std::make_shared<InferenceFailure>();
	failure->detail = context.typeFailureDetail;
	failure->configuredDetail = context.configuredTypeFailureDetail;
	failure->snapshot = context.typeFailureSnapshot;
	failure->relatedInfo = context.typeFailureRelatedInfo;
	failure->priority = context.typeFailurePriority;
	auto relative = [&](std::vector<RelatedInfo> &notes) {
		std::erase_if(notes, [&](const RelatedInfo &note) {
			return note.inferenceDepth >= 0 && note.inferenceDepth < base;
		});
		for (RelatedInfo &note : notes)
			if (note.inferenceDepth >= 0) {
				note.inferenceDepth -= base;
				note.inferenceOwnerBase = std::max(0, note.inferenceOwnerBase - base);
			}
	};
	relative(failure->relatedInfo);
	if (context.hasTypeFailureDiagnostic) {
		failure->diagnostic = context.typeFailureDiagnostic;
		failure->traced = std::ranges::any_of(failure->diagnostic->relatedInfo, [](const auto &note) {
			return note.inferenceDepth >= 0;
		});
		relative(failure->diagnostic->relatedInfo);
		failure->diagnostic->emission.reset();
	}
	return failure;
}

static void replayInferenceFailure(InferenceContext &context, const InferenceFailure &failure) {
	int base = static_cast<int>(context.diagnosticTraceDepth());
	auto rebase = [&](std::vector<RelatedInfo> &notes) {
		for (RelatedInfo &note : notes)
			if (note.inferenceDepth >= 0) {
				note.inferenceDepth += base;
				note.inferenceOwnerBase += base;
			}
	};
	context.typesValid = false;
	if (context.typeFailureDetail.empty() && !failure.detail.empty()) {
		context.typeFailureDetail = failure.detail;
		context.configuredTypeFailureDetail = failure.configuredDetail;
		context.typeFailureSnapshot = failure.snapshot;
		context.typeFailureRelatedInfo = failure.relatedInfo;
		rebase(context.typeFailureRelatedInfo);
		Diagnostic trace;
		context.appendCurrentInferenceTrace(trace);
		context.typeFailureRelatedInfo.insert(
			context.typeFailureRelatedInfo.end(), trace.relatedInfo.begin(), trace.relatedInfo.end()
		);
	}
	if (failure.diagnostic) {
		Diagnostic diagnostic = *failure.diagnostic;
		rebase(diagnostic.relatedInfo);
		context.fail(std::move(diagnostic), failure.priority, failure.traced);
	}
}
