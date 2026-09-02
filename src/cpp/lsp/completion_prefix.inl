struct CompletionPartial {
	std::string text;
};

struct CompletionPrefix {
	std::string normalized;
	std::string committed;
	std::optional<CompletionPartial> partial;
};

CompletionPrefix splitCompletionPrefix(const std::string &linePrefix) {
	CompletionPrefix result;
	result.normalized = normalizeCompletionPatternPrefix(linePrefix);
	result.committed = result.normalized;
	std::vector<PatternElement> elements = getPatternElements(result.normalized);
	if (result.normalized.empty() || std::isspace(static_cast<unsigned char>(result.normalized.back())) || elements.empty())
		return result;

	const PatternElement &last = elements.back();
	if (last.type != PatternElement::Type::VariableLike && last.type != PatternElement::Type::Other)
		return result;
	result.partial = CompletionPartial{last.text};
	result.committed = result.normalized.substr(0, last.startPos);
	return result;
}

void expandMultiWordVariableCompletionPrefix(
	const CompletionContext &context, SectionType sectionType, const std::set<std::string> &variableNames,
	CompletionPrefix &prefix
) {
	size_t longestPrefix = 0;
	SourceFile *sourceFile = completionSourceFile(context);
	for (const std::string &name : variableNames) {
		if (name.find(' ') == std::string::npos)
			continue;
		for (size_t start = 0; start < prefix.normalized.size(); start++) {
			if (std::isspace(static_cast<unsigned char>(prefix.normalized[start])) ||
				(start > 0 && !std::isspace(static_cast<unsigned char>(prefix.normalized[start - 1])))) {
				continue;
			}
			std::string_view entered = std::string_view(prefix.normalized).substr(start);
			if (entered.size() <= longestPrefix || !name.starts_with(entered))
				continue;
			std::optional<MatcherFrontier> frontier =
				collectMatcherFrontier(context, sectionType, prefix.normalized.substr(0, start));
			if (!frontier || !nodeAcceptsArgument(frontier->node, *sourceFile))
				continue;
			longestPrefix = entered.size();
			prefix.committed = prefix.normalized.substr(0, start);
			prefix.partial = CompletionPartial{std::string(entered)};
		}
	}
}

size_t completionSourceSuffixLength(const CompletionContext &context, std::string_view normalizedSuffix) {
	for (size_t start = 0; start < context.linePrefix.size(); start++) {
		if (std::isspace(static_cast<unsigned char>(context.linePrefix[start])))
			continue;
		if (normalizeCompletionWhitespace(std::string_view(context.linePrefix).substr(start)) == normalizedSuffix)
			return context.linePrefix.size() - start;
	}
	return normalizedSuffix.size();
}
