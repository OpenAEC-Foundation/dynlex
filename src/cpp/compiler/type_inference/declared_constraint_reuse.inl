// Align immutable prior products with current declarations before evaluating
// them in ordinary demand order. Pending queries have no reusable result.
static void alignDeclaredConstraintQueries(ParseContext &context, const DeclaredConstraintIdentities &identities) {
	if (context.priorConstraintQueries.empty())
		return;
	auto prior = std::move(context.priorConstraintQueries);
	context.priorConstraintQueries.resize(context.constraintQueries.size());
	for (size_t index = 0; index < context.constraintQueries.size(); index++)
		if (auto saved = identities.priorIndex(context.constraintQueries[index].identity))
			context.priorConstraintQueries[index] = std::move(prior[*saved]);
}

static bool canOmitReusableConstraintExpression(
	const ParseContext &context, const DeclaredTypeConstraintWorkItem &item, DeclaredConstraintIdentities &identities
) {
	auto index = identities.priorIndex(identities.identity(item));
	if (!index)
		return false;
	const auto &query = context.priorConstraintQueries.at(*index);
	using State = ParseContext::IncrementalConstraintQuery::State;
	return query.reusable && (query.value || query.state == State::Failed || query.state == State::Blocked);
}
