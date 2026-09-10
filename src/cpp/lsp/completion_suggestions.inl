struct PartialMatches {
	bool any = false;
	bool exact = false;

	void include(const PartialMatches &other) {
		any = any || other.any;
		exact = exact || other.exact;
	}
};

PatternFrontier describePatternFrontier(
	const MatcherFrontier &matcherFrontier, SectionType sectionType, const SourceFile &sourceFile,
	std::optional<std::string_view> partial
) {
	PatternFrontier result;
	result.patternKind = sectionTypeToString(sectionType);
	result.canComplete = !partial && nodeHasVisibleDefinition(matcherFrontier.node, sourceFile);

	if (matcherFrontier.node) {
		for (const auto &[literal, child] : matcherFrontier.node->literalChildren) {
			if (partial && !literal.starts_with(*partial))
				continue;
			std::string remaining = literal.substr(partial ? partial->size() : 0);
			if (!remaining.empty() && subtreeHasVisibleDefinition(child, sourceFile))
				result.transitions.push_back({"literal", std::move(remaining)});
		}
		if (!partial && nodeAcceptsArgument(matcherFrontier.node, sourceFile))
			result.transitions.push_back({"argument", {}});
	}

	std::sort(
		result.transitions.begin(), result.transitions.end(),
		[](const PatternFrontierTransition &left, const PatternFrontierTransition &right) {
		return std::tie(left.kind, left.text) < std::tie(right.kind, right.text);
	}
	);
	return result;
}

std::string patternFrontierKey(const PatternFrontier &frontier) {
	std::string key = frontier.patternKind;
	key += frontier.canComplete ? "\1" : "\0";
	for (const PatternFrontierTransition &transition : frontier.transitions) {
		key += transition.kind;
		key += '\0';
		key += transition.text;
		key += '\1';
	}
	return key;
}

void appendPatternFrontiers(
	const CompletionContext &context, SectionType sectionType, const CompletionPrefix &prefix,
	std::vector<PatternFrontier> &frontiers, std::set<std::string> &seen
) {
	CompletionPrefix effectivePrefix = prefix;
	expandMultiWordVariableCompletionPrefix(context, sectionType, visibleVariableNames(context), effectivePrefix);
	SourceFile *sourceFile = completionSourceFile(context);
	auto append = [&](const std::vector<MatcherFrontierCandidate> &candidates, std::optional<std::string_view> partial) {
		for (const MatcherFrontierCandidate &candidate : candidates) {
			PatternFrontier frontier = describePatternFrontier(candidate.frontier, sectionType, *sourceFile, partial);
			if (!frontier.canComplete && frontier.transitions.empty())
				continue;
			if (seen.insert(patternFrontierKey(frontier)).second)
				frontiers.push_back(std::move(frontier));
		}
	};

	append(
		collectMatcherFrontierCandidates(context, sectionType, effectivePrefix.committed),
		effectivePrefix.partial ? std::optional<std::string_view>(effectivePrefix.partial->text) : std::nullopt
	);
	if (effectivePrefix.partial)
		append(collectMatcherFrontierCandidates(context, sectionType, effectivePrefix.normalized), std::nullopt);
}

PartialMatches collectPartialLiteralSuggestions(
	PatternTreeNode *node, std::string_view partial, std::vector<CompletionItem> &items, std::set<std::string> &seen,
	std::string_view detailPrefix, std::string_view sortPrefix, const SourceFile &sourceFile, const CompletionContext &context
) {
	PartialMatches matches;
	if (!node)
		return matches;

	std::vector<std::string> literals;
	for (const auto &[literal, child] : node->literalChildren) {
		if (!literal.starts_with(partial) || !subtreeHasVisibleDefinition(child, sourceFile))
			continue;
		matches.any = true;
		std::string suggestion = extendLiteralSuggestion(literal, child);
		if (literal.size() == partial.size()) {
			matches.exact = true;
			if (suggestion == literal)
				continue;
		}
		literals.push_back(std::move(suggestion));
	}

	std::sort(literals.begin(), literals.end());
	for (const std::string &literal : literals) {
		TextEdit textEdit;
		textEdit.range = makeRange(context.line, context.character - static_cast<int>(partial.size()), context.character);
		textEdit.newText = literal;
		addCompletionItem(
			items, seen, literal, CompletionItemKind::Keyword, std::string(detailPrefix), literal,
			std::string(sortPrefix) + literal, std::move(textEdit)
		);
	}
	return matches;
}

PartialMatches collectPartialVariableSuggestions(
	PatternTreeNode *node, const std::set<std::string> &variableNames, std::string_view partial,
	std::vector<CompletionItem> &items, std::set<std::string> &seen, const SourceFile &sourceFile,
	const CompletionContext &context
) {
	PartialMatches matches;
	if (!nodeAcceptsArgument(node, sourceFile))
		return matches;
	for (const std::string &name : variableNames) {
		if (!name.starts_with(partial))
			continue;
		matches.any = true;
		matches.exact = matches.exact || name.size() == partial.size();
	}
	collectVariableSuggestions(
		node, variableNames, items, seen, sourceFile, context, partial, completionSourceSuffixLength(context, partial)
	);
	return matches;
}

void collectStableFrontierSuggestions(
	const MatcherFrontier &frontier, std::string_view detailPrefix, const CompletionContext &context,
	std::vector<CompletionItem> &items, std::set<std::string> &seenLabels
) {
	SourceFile *sourceFile = completionSourceFile(context);
	TextEdit insertionEdit;
	insertionEdit.range = makeRange(context.line, context.character, context.character);

	collectNextLiteralSuggestions(
		frontier.node, items, seenLabels, std::string(detailPrefix) + " next token", "0_", *sourceFile, insertionEdit
	);
	if (!nodeAcceptsArgument(frontier.node, *sourceFile))
		return;

	PatternTreeNode *functionRoot = context.parseContext->patternTrees[(int)SectionType::Function];
	requireCompilerInvariant(functionRoot != nullptr, "completion substitution requires a function pattern tree");
	collectNextLiteralSuggestions(
		functionRoot, items, seenLabels, "function pattern substitution", "1_", *sourceFile, insertionEdit
	);

	std::set<std::string> variableNames = visibleVariableNames(context);
	variableNames.insert(frontier.acceptedVariables.begin(), frontier.acceptedVariables.end());
	collectVariableSuggestions(frontier.node, variableNames, items, seenLabels, *sourceFile, context);
}

void collectMatcherFrontierCompletions(
	SectionType sectionType, std::string_view detailPrefix, const CompletionContext &context, const std::string &linePrefix,
	std::vector<CompletionItem> &items, std::set<std::string> &seenLabels
) {
	CompletionPrefix prefix = splitCompletionPrefix(linePrefix);
	const auto variableNames = visibleVariableNames(context);
	expandMultiWordVariableCompletionPrefix(context, sectionType, variableNames, prefix);
	auto stable = [&](const MatcherFrontier &frontier) {
		collectStableFrontierSuggestions(frontier, detailPrefix, context, items, seenLabels);
		return true;
	};
	if (!prefix.partial) {
		visitCompletionFrontiers(context, sectionType, prefix.committed, stable);
		return;
	}

	SourceFile *sourceFile = completionSourceFile(context);
	PartialMatches matches;
	visitCompletionFrontiers(context, sectionType, prefix.committed, [&](const MatcherFrontier &frontier) {
		PartialMatches current = collectPartialLiteralSuggestions(
			frontier.node, prefix.partial->text, items, seenLabels, std::string(detailPrefix) + " token", "0_", *sourceFile,
			context
		);
		if (nodeAcceptsArgument(frontier.node, *sourceFile)) {
			PatternTreeNode *functionRoot = context.parseContext->patternTrees[(int)SectionType::Function];
			requireCompilerInvariant(functionRoot != nullptr, "completion substitution requires a function pattern tree");
			current.include(collectPartialLiteralSuggestions(
				functionRoot, prefix.partial->text, items, seenLabels, "function pattern substitution", "1_", *sourceFile,
				context
			));
			auto names = variableNames;
			names.insert(frontier.acceptedVariables.begin(), frontier.acceptedVariables.end());
			current.include(collectPartialVariableSuggestions(
				frontier.node, names, prefix.partial->text, items, seenLabels, *sourceFile, context
			));
		}
		matches.include(current);
		return current.any;
	});
	if (!matches.any || matches.exact)
		visitCompletionFrontiers(context, sectionType, prefix.normalized, stable);
}
