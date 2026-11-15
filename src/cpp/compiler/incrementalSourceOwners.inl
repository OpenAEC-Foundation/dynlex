// Source-only eligibility runs after section/declaration production and before
// the resolver collects concrete references. Dependency proofs remain owned by
// classification and the initial/final matcher stages below.
context.prepareSourceOutlines = [&]() {
	IncrementalWorkTimer timer(showStatistics ? &statistics.matcherProjection.sourceOwnerSelection : nullptr);
	std::unordered_map<Section *, std::vector<BodySyntaxProduct *>> owners;
	for (const auto &product : context.sourceSyntaxProducts)
		owners[const_cast<Section *>(&product->bodyOwner())].push_back(product.get());
	const Json *prior = frontendState && compatibleObservations(*frontendState, recording->observations, context)
							? &frontendState->at("all_function_projection")
							: nullptr;
	const auto &bodies = initialParameterCounts.bodies();
	for (auto &[owner, products] : owners) {
		bool eligible = prior && !context.options.emitDebugInfo && !context.options.traceExecution &&
						owner->parent == context.mainSection && owner->type == SectionType::Function && !owner->isFlex &&
						!owner->declarationProducer && !containsNestedDefinition(*owner) && hasNoGlobalAccess(owner) &&
						sourceOwnerReferences(*owner).empty();
		const std::string &path = initialParameterCounts.path(*owner);
		const std::vector<IncrementalMatcherCache::ReferenceMatchKeys> *references = nullptr;
		if (eligible) {
			const Json &matcher = prior->at("matcher_projection");
			eligible = matcher.at("bodies").contains(path) && matcher.at("bodies").at(path) == bodies.at(path) &&
					   matcher.at("body_units").contains(path) && matcher.at("final_body_units").contains(path) &&
					   matcherCache.contains(matcher.at("body_units").at(path).get_ref<const std::string &>()) &&
					   matcherCache.contains(matcher.at("final_body_units").at(path).get_ref<const std::string &>());
			const Json &diagnostics = frontendState->at("frontend_diagnostic_units");
			eligible &= diagnostics.empty() || !diagnostics.contains(digest(sectionIdentity(*owner).dump()));
			if (eligible) {
				references = savedReferenceKeys(*prior, matcherCache, path);
				eligible = references && !references->empty();
			}
		}
		if (eligible)
			context.retainedSourceOwners.emplace(owner, RetainedSourceOwner{std::move(products), references->size()});
		else
			for (BodySyntaxProduct *product : products)
				product->prepareReferences();
	}
};
