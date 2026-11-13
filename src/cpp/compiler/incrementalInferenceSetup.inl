if (previous) {
	auto payloadStart = Clock::now();
	for (const char *field : {"section_dependency_graph", "inference_summaries", "generated_ir"})
		statistics.cachePayloadsLoaded += hydrateProgramField(*previous, directory, field);
	statistics.cachePayloadLoadMilliseconds += elapsedMilliseconds(payloadStart);
}
priorInference =
	selectInferenceState(previous ? &*previous : nullptr, previousFailure ? &*previousFailure : nullptr, directory, statistics);
if (priorInference) {
	const Json &prior = priorInference->at("all_function_projection");
	const Json &graph = priorInference->at("section_dependency_graph");
	if (prior.at("final_matcher_tree") != finalMatcherTree ||
		prior.at("declarations") != resolvedFunctionProjection.at("declarations"))
		inferenceReads.validate(context, graph.at("callable_lookups"));
}
auto constraintPrepareStart = Clock::now();
priorConstraints =
	selectConstraintReuseState(priorInference, previousFailure ? &*previousFailure : nullptr, directory, statistics);
if (priorConstraints.source && compatibleObservations(*priorConstraints.source, recording->observations, context)) {
	auto changed = inferenceReads.changedKeys();
	if (!priorInference || priorConstraints.graph != &priorInference->at("section_dependency_graph"))
		changed = inferenceReads.validate(context, priorConstraints.graph->at("callable_lookups"));
	prepareConstraintProbeReuse(
		context, *priorConstraints.projection, resolvedFunctionProjection, *priorConstraints.graph, sections, changed
	);
}
statistics.constraintPrepareMilliseconds += elapsedMilliseconds(constraintPrepareStart);
if (priorInference) {
	auto controller = std::make_shared<IncrementalInferenceController>(
		*priorInference, previous ? &*previous : nullptr, recording->observations, resolvedFunctionProjection, sectionPaths,
		inferenceReads, frontendDiagnosticRecords, statistics, functionReuse, inferenceReuse, graphStable,
		inferenceInputFingerprints, summaryStore, &products.references(), products.sourceIndex(), products.globalInputs(),
		products
	);
	context.runIncrementalPreinference = [controller](
											 ParseContext &inferred,
											 const std::function<bool(const ParseContext::IncrementalFunctionRequest &)> &infer
										 ) {
		controller->run(inferred, infer);
	};
}
