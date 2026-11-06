// Matching first discovers declarations and implicit parameters. Once syntax
// families are stable, apply their partial order to the complete matches before
// committing expression trees and variable identities.
static bool samePatternMatchSyntax(const PatternMatch &left, const PatternMatch &right) {
	if (left.matchedEndNode != right.matchedEndNode || left.matchingDefinitions != right.matchingDefinitions ||
		left.nodesPassed != right.nodesPassed || left.lineStartPos != right.lineStartPos ||
		left.lineEndPos != right.lineEndPos || left.discoveredVariables.size() != right.discoveredVariables.size() ||
		left.acceptedLiterals.size() != right.acceptedLiterals.size() || left.subMatches.size() != right.subMatches.size() ||
		left.orderedArguments.size() != right.orderedArguments.size())
		return false;
	for (size_t index = 0; index < left.discoveredVariables.size(); index++) {
		const VariableMatch &a = left.discoveredVariables[index], &b = right.discoveredVariables[index];
		if (a.name != b.name || a.lineStartPos != b.lineStartPos || a.lineEndPos != b.lineEndPos)
			return false;
	}
	for (size_t index = 0; index < left.acceptedLiterals.size(); index++)
		if (left.acceptedLiterals[index].node != right.acceptedLiterals[index].node)
			return false;
	for (size_t index = 0; index < left.orderedArguments.size(); index++) {
		const MatchedArgument &a = left.orderedArguments[index], &b = right.orderedArguments[index];
		if (a.argumentIndex != b.argumentIndex || a.kind != b.kind || a.expression != b.expression ||
			a.itemIndex != b.itemIndex || a.isPackElement != b.isPackElement)
			return false;
	}
	for (size_t index = 0; index < left.subMatches.size(); index++)
		if (!samePatternMatchSyntax(left.subMatches[index], right.subMatches[index]))
			return false;
	return true;
}

using PrecedenceMatchChanges = std::vector<std::pair<PatternReference *, std::unique_ptr<PatternMatch>>>;

static bool matchMayChangeWithPrecedence(const PatternMatch &match) {
	// The precedence pass only rejects matched definitions that outrank an operand.
	for (const PatternDefinition *definition : match.matchingDefinitions) {
		requireCompilerInvariant(definition->family != nullptr, "precedence candidate has no syntax family");
		if (!definition->family->precedenceSuccessors.empty())
			return true;
	}
	for (const PatternMatch &subMatch : match.subMatches)
		if (matchMayChangeWithPrecedence(subMatch))
			return true;
	return false;
}

static bool collectPrecedenceMatchChanges(ParseContext &context, PrecedenceMatchChanges &changes) {
	std::list<PatternReference *> bodyReferences, globalReferences;
	std::list<Section *> sections;
	context.mainSection->collectPatternReferencesAndSections(bodyReferences, globalReferences, sections);
	bodyReferences.splice(bodyReferences.end(), globalReferences);
	bodyReferences.sort(referenceComesBefore);
	for (PatternReference *reference : bodyReferences) {
		if (!reference->match)
			continue; // Single-variable references have no matched function.
		if (!matchMayChangeWithPrecedence(*reference->match))
			continue;
		auto match = std::unique_ptr<PatternMatch>(context.match(reference));
		if (!match) {
			context.addDiagnostic(Diagnostic(context, Diagnostic::Level::Error, "unresolved pattern", reference->range()));
			return false;
		}
		if (!samePatternMatchSyntax(*reference->match, *match))
			changes.emplace_back(reference, std::move(match));
	}
	return true;
}

static void clearPatternFamilies(ParseContext &context) {
	std::function<void(Section *)> clear = [&](Section *section) {
		for (PatternDefinition *definition : section->patternDefinitions)
			definition->family = nullptr;
		for (Section *child : section->children)
			clear(child);
	};
	clear(context.mainSection);
	context.patternFamilies.clear();
}
