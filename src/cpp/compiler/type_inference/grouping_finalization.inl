#pragma once

static bool finalizeDeferredGroupingAmbiguities(ParseContext &parseContext) {
	for (size_t deferredIndex = 0; deferredIndex < parseContext.deferredGroupingAmbiguities.size(); deferredIndex++) {
		ParseContext::DeferredGroupingAmbiguity deferred = parseContext.deferredGroupingAmbiguities[deferredIndex];
		if (!deferred.line)
			crashCompilerBug("deferred grouping ambiguity has no code line");
		if (deferred.line->groupingAmbiguityChecked)
			continue;
		if (!deferred.rootSection)
			crashCompilerBug("deferred grouping ambiguity has no owning section");

		InferenceContext context(parseContext);
		context.detectGroupingAmbiguity = true;
		std::shared_ptr<InstantiatedSectionBody> body;
		ScopedSectionLocalVariableState localVariableState(deferred.rootSection);
		if (deferred.instantiation) {
			if (!deferred.instantiation->valid || deferred.instantiation->needsReinfer ||
				!deferred.instantiation->returnType.isDeduced()) {
				crashCompilerBug("deferred grouping ambiguity reached finalization with an unstable function type");
			}
			if (deferred.instantiation->inferring)
				crashCompilerBug("deferred grouping ambiguity reached finalization during function inference");
			body = deferred.instantiation->body;
			if (!body)
				crashCompilerBug("deferred grouping ambiguity has no instantiated function body");
			context.currentInstantiation = deferred.instantiation;
			deferred.instantiation->inferring = true;
		}

		bool inferred = inferSection(deferred.rootSection, body.get(), nullptr, context, {});
		if (deferred.instantiation)
			deferred.instantiation->inferring = false;
		if (!inferred || !context.typesValid)
			return false;
		if (!deferred.line->groupingAmbiguityChecked)
			crashCompilerBug("stable return types did not complete deferred grouping ambiguity inference");
	}
	return true;
}
