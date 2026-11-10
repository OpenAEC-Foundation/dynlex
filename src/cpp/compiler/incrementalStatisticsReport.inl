struct BodyReferenceOwnership {
	size_t instantiated = 0;
	size_t uninstantiated = 0;
	size_t flex = 0;
	size_t conversion = 0;
	size_t other = 0;
};

BodyReferenceOwnership bodyReferenceOwnership(const Section *root) {
	BodyReferenceOwnership counts;
	if (!root)
		return counts;
	auto visit = [&](auto &&self, const Section *section, const Section *owner) -> void {
		if (!section->patternDefinitions.empty())
			owner = section;
		if (owner && owner->type == SectionType::Function) {
			if (owner->isFlex)
				counts.flex += section->patternReferences.size();
			else if (owner->isConversion)
				counts.conversion += section->patternReferences.size();
			else if (owner->instantiations.empty())
				counts.uninstantiated += section->patternReferences.size();
			else
				counts.instantiated += section->patternReferences.size();
		} else if (owner) {
			counts.other += section->patternReferences.size();
		}
		for (const Section *child : section->children)
			self(self, child, owner);
	};
	visit(visit, root, nullptr);
	return counts;
}

template <typename Statistics> void emitIncrementalStatistics(const ParseContext &context, const Statistics &statistics) {
	BodyReferenceOwnership bodyReferences = bodyReferenceOwnership(context.mainSection);
	std::cerr << "incremental: "
			  << Json{{"cache_directory", statistics.cacheDirectory},
					  {"cache_entry", statistics.cacheEntry},
					  {"frontend_reused", statistics.reusedFrontend},
					  {"type_inference_reused", statistics.reusedTypeInference},
					  {"inference_instantiations_reused", statistics.reusedInferenceInstantiations},
					  {"pure_value_summary_hits", context.pureValueSummaryHits},
					  {"inference_summary_sections_reused", statistics.reusedInferenceSummarySections},
					  {"preinferred_instantiations", statistics.preinferredInstantiations},
					  {"functions_ir_reused", statistics.reusedFunctionIR},
					  {"dependency_graph_sections_reused", statistics.dependencyGraphSectionsReused},
					  {"preinfer_dependency_graph_sections_checked", statistics.preinferDependencyGraphSectionsChecked},
					  {"dependency_graph_entries_reused", statistics.dependencyGraphEntriesReused},
					  {"dependency_graph_reverse_nodes_updated", statistics.dependencyGraphReverseNodesUpdated},
					  {"diagnostic_records_reused", statistics.reusedDiagnosticRecords},
					  {"linked_cached_objects", statistics.linkedCachedObjects},
					  {"objects_reused", statistics.reusedObjects},
					  {"objects_built", statistics.builtObjects},
					  {"matcher_cache_hits", statistics.matcherCacheHits},
					  {"deferred_match_keys_replayed", statistics.deferredMatchKeysReplayed},
					  {"precedence_match_keys_replayed", statistics.precedenceMatchKeysReplayed},
					  {"precedence_matcher_cache_hits", statistics.precedenceMatcherCacheHits},
					  {"expression_parse_hits", statistics.expressionParseHits},
					  {"temporary_expression_parse_hits", statistics.temporaryExpressionParseHits},
					  {"constraint_probes_reused", context.constraintProbesReused},
					  {"constraint_probes_run", context.constraintProbesRun},
					  {"constraint_values_reused", context.constraintValuesReused},
					  {"constraint_expressions_reused", context.constraintExpressionsReused},
					  {"expression_parse_misses", statistics.expressionParseMisses},
					  {"definition_parse_hits", statistics.definitionParseHits},
					  {"definition_parse_misses", statistics.definitionParseMisses},
					  {"body_resolution_reference_visits", context.bodyResolutionReferenceVisits},
					  {"inactive_body_resolution_references_skipped", context.inactiveBodyResolutionReferencesSkipped},
					  {"inactive_body_selection_ms", statistics.inactiveBodySelectionMilliseconds},
					  {"matcher_projection_ms", statistics.matcherProjectionMilliseconds},
					  {"body_references_in_instantiated_functions", bodyReferences.instantiated},
					  {"body_references_in_uninstantiated_functions", bodyReferences.uninstantiated},
					  {"body_references_in_flex_functions", bodyReferences.flex},
					  {"body_references_in_conversion_functions", bodyReferences.conversion},
					  {"body_references_in_other_definitions", bodyReferences.other},
					  {"body_resolution_match_cache_hits", context.bodyResolutionMatchCacheHits},
					  {"body_resolution_match_computations", context.bodyResolutionMatchComputations},
					  {"variable_like_count_references_scanned", context.variableLikeCountReferencesScanned},
					  {"resolution_setup_ms", context.resolutionSetupMilliseconds},
					  {"resolution_definition_ms", context.resolutionDefinitionMilliseconds},
					  {"resolution_body_ms", context.resolutionBodyMilliseconds},
					  {"resolution_precedence_ms", context.resolutionPrecedenceMilliseconds},
					  {"resolution_rematch_ms", context.resolutionRematchMilliseconds},
					  {"resolution_candidate_collection_ms", context.resolutionCandidateCollectionMilliseconds},
					  {"resolution_candidate_sort_ms", context.resolutionCandidateSortMilliseconds},
					  {"resolution_replay_setup_ms", context.resolutionReplaySetupMilliseconds},
					  {"resolution_candidate_match_ms", context.resolutionCandidateMatchMilliseconds},
					  {"precedence_graph_edges", context.patternPrecedenceGraphEdges},
					  {"source_projections_reused", statistics.sourceProjectionWork.reused},
					  {"source_projections_scanned", statistics.sourceProjectionWork.scanned},
					  {"source_projection_mask_ms", statistics.sourceProjectionWork.maskMilliseconds},
					  {"source_projection_files_ms", statistics.sourceProjectionWork.sourceMilliseconds},
					  {"projection_setup_ms", statistics.sourceProjectionWork.setupMilliseconds},
					  {"projection_body_ms", statistics.sourceProjectionWork.bodyMilliseconds},
					  {"projection_declarations_ms", statistics.sourceProjectionWork.declarationMilliseconds},
					  {"projection_matches_ms", statistics.sourceProjectionWork.matchMilliseconds},
					  {"projection_global_matches_reused", statistics.sourceProjectionWork.globalMatchesReused},
					  {"projection_top_level_sections_reused", statistics.topLevelProjectionSectionsReused},
					  {"projection_match_entries_reused", statistics.sourceProjectionWork.matchEntriesReused},
					  {"projection_declaration_sections_reused", statistics.sourceProjectionWork.declarationSectionsReused},
					  {"projection_inactive_ms", statistics.sourceProjectionWork.inactiveMilliseconds},
					  {"projection_ambient_ms", statistics.sourceProjectionWork.ambientMilliseconds},
					  {"projection_compatibility_ms", statistics.sourceProjectionWork.compatibilityMilliseconds},
					  {"matcher_key_ms", statistics.matcherKeyMilliseconds},
					  {"matcher_restore_ms", statistics.matcherRestoreMilliseconds},
					  {"precedence_tree_signature_ms", statistics.precedenceTreeSignatureMilliseconds},
					  {"precedence_signature_ms", statistics.precedenceSignatureMilliseconds},
					  {"precedence_rule_signature_ms", statistics.precedenceRuleSignatureMilliseconds},
					  {"precedence_signature_reused", statistics.precedenceSignaturesReused},
					  {"cache_load_ms", statistics.cacheLoadMilliseconds},
					  {"cache_payload_load_ms", statistics.cachePayloadLoadMilliseconds},
					  {"cache_payloads_loaded", statistics.cachePayloadsLoaded},
					  {"cache_restore_ms", statistics.cacheRestoreMilliseconds},
					  {"cache_finalize_ms", statistics.cacheFinalizeMilliseconds},
					  {"total_ms", statistics.totalMilliseconds},
					  {"frontend_ms", statistics.frontendMilliseconds},
					  {"import_ms", context.frontendStageMilliseconds[0]},
					  {"parse_ms", context.frontendStageMilliseconds[1]},
					  {"resolve_ms", context.frontendStageMilliseconds[2]},
					  {"validate_target_ms", context.frontendStageMilliseconds[3]},
					  {"inference_ms", statistics.inferenceMilliseconds},
					  {"inference_stage_ms", context.inferenceStageMilliseconds},
					  {"projection_ms", statistics.projectionMilliseconds},
					  {"inactive_projection_ms", statistics.inactiveProjectionMilliseconds},
					  {"inactive_projection_matches_rechecked", statistics.inactiveProjectionMatchesRechecked},
					  {"parser_snapshot_ms", statistics.parserSnapshotMilliseconds},
					  {"matcher_snapshot_ms", statistics.matcherSnapshotMilliseconds},
					  {"dependency_graph_ms", statistics.dependencyGraphMilliseconds},
					  {"preinference_ms", statistics.preinferenceMilliseconds},
					  {"ir_generation_ms", statistics.irGenerationMilliseconds},
					  {"unoptimized_ir_snapshot_ms", statistics.unoptimizedIRSnapshotMilliseconds},
					  {"optimization_ms", statistics.optimizationMilliseconds},
					  {"optimization_input_units_built", statistics.optimizationInputUnitsBuilt},
					  {"optimization_input_units_reused", statistics.optimizationInputUnitsReused},
					  {"optimization_units_built", statistics.optimizationUnitsBuilt},
					  {"optimization_units_reused", statistics.optimizationUnitsReused},
					  {"optimization_input_ms", statistics.optimizationInputMilliseconds},
					  {"optimization_combine_ms", statistics.optimizationCombineMilliseconds},
					  {"snapshot_ms", statistics.snapshotMilliseconds},
					  {"cache_write_ms", statistics.cacheWriteMilliseconds},
					  {"partition_ms", statistics.partitionMilliseconds},
					  {"native_dependency_ms", statistics.nativeDependencyMilliseconds},
					  {"native_partition_modules_built", statistics.nativePartitionModulesBuilt},
					  {"native_partition_modules_skipped", statistics.nativePartitionModulesSkipped},
					  {"native_function_dependencies_reused", statistics.nativeFunctionDependenciesReused},
					  {"native_function_fingerprints_computed", statistics.nativeFunctionFingerprintsComputed},
					  {"link_ms", statistics.linkMilliseconds},
					  {"codegen_and_link_ms", statistics.codegenMilliseconds}}
					 .dump()
			  << '\n';
}
