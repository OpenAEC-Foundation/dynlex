// Match saved queries by identity before restoring their readiness schedule.
// Added or removed declarations do not shift another query's result or reads.
static void alignDeclaredConstraintQueries(ParseContext &context, const DeclaredConstraintIdentities &identities) {
	if (!context.reuseDeclaredConstraintProbes)
		return;
	size_t saved = context.priorConstraintProbeItems.size();
	requireCompilerInvariant(
		context.priorConstraintProbePasses.size() == saved && context.priorConstraintProbeReusable.size() == saved &&
			context.priorConstraintProbeValues.size() == saved && context.priorConstraintProbeDependencies.size() == saved,
		"saved constraint query products have inconsistent sizes"
	);
	auto passes = std::move(context.priorConstraintProbePasses);
	auto reusable = std::move(context.priorConstraintProbeReusable);
	auto values = std::move(context.priorConstraintProbeValues);
	auto dependencies = std::move(context.priorConstraintProbeDependencies);
	size_t count = context.constraintProbeItems.size();
	context.priorConstraintProbeItems = context.constraintProbeItems;
	context.priorConstraintProbePasses.assign(count, 0);
	context.priorConstraintProbeReusable.assign(count, false);
	context.priorConstraintProbeValues.assign(count, std::nullopt);
	context.priorConstraintProbeDependencies.assign(count, {});
	for (size_t index = 0; index < count; index++) {
		auto prior = identities.priorIndex(context.constraintProbeItems[index]);
		if (!prior)
			continue;
		context.priorConstraintProbePasses[index] = passes[*prior];
		context.priorConstraintProbeReusable[index] = reusable[*prior];
		context.priorConstraintProbeValues[index] = std::move(values[*prior]);
		context.priorConstraintProbeDependencies[index] = std::move(dependencies[*prior]);
	}
}

static bool canOmitReusableConstraintExpression(
	const ParseContext &context, const DeclaredTypeConstraintWorkItem &item, DeclaredConstraintIdentities &identities
) {
	if (!context.reuseDeclaredConstraintProbes)
		return false;
	auto index = identities.priorIndex(identities.identity(item));
	return index && context.priorConstraintProbeReusable.at(*index) && context.priorConstraintProbeValues.at(*index);
}
