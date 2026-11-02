static bool consumePackText(MatchProgress &progress, std::string_view text) {
	for (char character : text) {
		auto element =
			remainingPatternElement(progress.patternReference, progress.sourceElementIndex, progress.sourceCharIndex);
		if (!element.exists || element.type == PatternElement::Variable || element.text.front() != character)
			return false;
		progress.patternPos++;
		if (element.text.size() == 1) {
			progress.sourceElementIndex++;
			progress.sourceCharIndex = 0;
		} else {
			progress.sourceCharIndex++;
		}
	}
	return true;
}

static void consumePackSpaces(MatchProgress &progress) {
	while (true) {
		MatchProgress next = progress;
		if (!consumePackText(next, " "))
			break;
		progress = std::move(next);
	}
}

static std::optional<MatchProgress> packContinuation(const MatchProgress &progress, bool final) {
	if (!progress.pack.captureNode || progress.pack.expectsItem || progress.pack.finalSeparator || progress.pack.singleOnly)
		return std::nullopt;
	const auto &syntax = syntaxConfigForSourceFile(*progress.context, progress.patternReference->range().line->sourceFile);
	MatchProgress next = progress;
	consumePackSpaces(next);
	if (!consumePackText(next, final ? syntax.argumentPackConjunction : syntax.argumentPackSeparator))
		return std::nullopt;
	// The conjunction is a word, so require its trailing separator.
	if (final && !consumePackText(next, " "))
		return std::nullopt;
	consumePackSpaces(next);
	if (!final) {
		MatchProgress oxford = next;
		if (consumePackText(oxford, syntax.argumentPackConjunction) && consumePackText(oxford, " ")) {
			consumePackSpaces(oxford);
			next = std::move(oxford);
			final = true;
		}
	}
	next.pack.finalSeparator = final;
	next.pack.expectsItem = true;
	return next;
}

static bool packCanEnd(const PackMatchState &pack) {
	return !pack.captureNode ||
		   (!pack.expectsItem && pack.count >= pack.minimumCount && (pack.count == 1 || pack.finalSeparator));
}

void MatchProgress::appendArgument(MatchStorage &storage, MatchedArgument argument) {
	if (pack.captureNode && pack.expectsItem) {
		argument.argumentIndex = pack.argumentIndex;
		argument.isPackElement = true;
		if (pack.count == 0)
			matchedArgumentIndex++;
		pack.count++;
		pack.expectsItem = false;
	} else {
		argument.argumentIndex = matchedArgumentIndex++;
	}
	storage.append(match.orderedArguments, std::move(argument));
}
