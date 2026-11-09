struct SourceProjectionWork {
	size_t reused = 0;
	size_t scanned = 0;
	double maskMilliseconds = 0;
	double sourceMilliseconds = 0;
	double setupMilliseconds = 0;
	double bodyMilliseconds = 0;
	double declarationMilliseconds = 0;
	double matchMilliseconds = 0;
	double inactiveMilliseconds = 0;
	double ambientMilliseconds = 0;
	double compatibilityMilliseconds = 0;
};

Json sourceProjection(
	const ParseContext &context, const Json &observations, const std::unordered_set<const Section *> &inactive,
	bool maskOpeningLines = false, bool ignoreMaskedRuns = false, bool maskGlobalExecution = false,
	const Json *priorSources = nullptr, SourceProjectionWork *work = nullptr
) {
	auto maskStart = Clock::now();
	using SourceRanges = std::map<const lsp::SourceFile *, std::map<int, std::vector<std::pair<size_t, size_t>>>>;
	std::map<const lsp::SourceFile *, std::set<int>> masked;
	std::map<const lsp::SourceFile *, std::set<int>> preserved;
	SourceRanges maskedRanges;
	std::unordered_map<const lsp::SourceFile *, std::string> maskTraces;
	auto appendNumber = [](std::string &output, std::uint64_t value) {
		for (size_t index = 0; index < 8; index++) {
			output.push_back(static_cast<char>(value & 0xff));
			value >>= 8;
		}
	};
	auto traceFor = [&](const lsp::SourceFile *source) -> std::string & {
		auto [found, inserted] = maskTraces.try_emplace(source);
		if (inserted) {
			std::string &trace = found->second;
			trace += maskOpeningLines ? '1' : '0';
			trace += ignoreMaskedRuns ? '1' : '0';
			trace += maskGlobalExecution ? '1' : '0';
			const std::string &commentPrefix =
				source->isInternal ? context.builtinSyntax.commentPrefix : context.projectSyntax.commentPrefix;
			appendNumber(trace, commentPrefix.size());
			trace += commentPrefix;
		}
		return found->second;
	};
	std::unordered_set<const CodeLine *> openings;
	if (maskOpeningLines)
		for (const Section *section : inactive)
			if (section->openingLine)
				openings.insert(section->openingLine);
	for (const CodeLine *line : context.codeLines) {
		bool globalExecution = maskGlobalExecution && (!line->sectionOpening || line->sectionOpening->runsInEnclosingBody());
		if (globalExecution)
			for (const Section *section = line->section; section && section != context.mainSection; section = section->parent)
				if (!section->runsInEnclosingBody()) {
					globalExecution = false;
					break;
				}
		bool insideInactive = openings.contains(line) || globalExecution;
		for (const Section *section = line->section; section; section = section->parent) {
			if (inactive.contains(section)) {
				insideInactive = true;
				break;
			}
		}
		(insideInactive ? masked : preserved)[line->sourceFile].insert(line->sourceFileLineIndex);
		std::string &lineTrace = traceFor(line->sourceFile);
		lineTrace.push_back(insideInactive ? 'M' : 'P');
		appendNumber(lineTrace, static_cast<std::uint64_t>(line->sourceFileLineIndex));
		if (!insideInactive)
			continue;
		int bodyStart = line->hasIndentOverride ? static_cast<int>(line->indentOverride.size()) : 0;
		int bodyEnd = static_cast<int>(line->rightTrimmedText.size());
		for (const SourceSlice &slice : line->sourceSlices) {
			int from = std::max(bodyStart, slice.transformedStart);
			int to = std::min(bodyEnd, slice.transformedEnd);
			if (to <= from)
				continue;
			size_t sourceStart = static_cast<size_t>(slice.sourceColumnStart + from - slice.transformedStart);
			maskedRanges[slice.sourceFile][slice.sourceFileLineIndex].emplace_back(sourceStart, sourceStart + to - from);
			std::string &sliceTrace = traceFor(slice.sourceFile);
			sliceTrace.push_back('R');
			appendNumber(sliceTrace, static_cast<std::uint64_t>(slice.sourceFileLineIndex));
			appendNumber(sliceTrace, sourceStart);
			appendNumber(sliceTrace, sourceStart + to - from);
		}
	}
	if (work)
		work->maskMilliseconds += elapsedMilliseconds(maskStart);
	auto sourceStart = Clock::now();
	Json sources = Json::object();
	for (const auto &[_, source] : context.importedFiles) {
		const std::string &commentPrefix =
			source->isInternal ? context.builtinSyntax.commentPrefix : context.projectSyntax.commentPrefix;
		std::string maskDigest = digest(traceFor(source));
		const std::string &contentDigest = observations.at(source->uri).at("content").get_ref<const std::string &>();
		if (priorSources && priorSources->contains(source->uri)) {
			const Json &prior = priorSources->at(source->uri);
			if (prior.at("content") == contentDigest && prior.at("mask") == maskDigest) {
				sources[source->uri] = prior;
				if (work)
					work->reused++;
				continue;
			}
		}
		if (work)
			work->scanned++;
		std::string projection;
		std::string_view content = source->content;
		size_t start = 0;
		int lineIndex = 0;
		bool inMaskedRun = false;
		auto appendPreserved = [&](std::string_view part) {
			if (part.empty())
				return;
			projection += 'L';
			projection += std::to_string(part.size());
			projection += ':';
			projection.append(part);
			inMaskedRun = false;
		};
		auto appendMasked = [&]() {
			if (ignoreMaskedRuns)
				return;
			if (!inMaskedRun)
				projection += 'M';
			inMaskedRun = true;
		};
		while (start < content.size()) {
			size_t end = content.find('\n', start);
			if (end == std::string_view::npos)
				end = content.size();
			else
				end++;
			std::string_view semanticLine = content.substr(start, end - start);
			if (semanticLine.ends_with('\n'))
				semanticLine.remove_suffix(1);
			if (size_t comment = findCommentStart(semanticLine, commentPrefix); comment != std::string_view::npos)
				semanticLine = semanticLine.substr(0, comment);
			while (!semanticLine.empty() && std::isspace(static_cast<unsigned char>(semanticLine.back())))
				semanticLine.remove_suffix(1);
			if (semanticLine.empty()) {
				start = end;
				lineIndex++;
				continue;
			}
			std::string normalizedLine(semanticLine);
			normalizedLine.push_back('\n');
			std::string_view line(normalizedLine);
			bool hasMask = masked[source].contains(lineIndex);
			bool hasPreserved = preserved[source].contains(lineIndex);
			if (hasMask && !hasPreserved) {
				appendMasked();
			} else if (hasMask && hasPreserved) {
				auto ranges = maskedRanges[source][lineIndex];
				std::sort(ranges.begin(), ranges.end());
				size_t cursor = 0;
				for (const auto &[rangeStart, rangeEnd] : ranges) {
					size_t from = std::min(rangeStart, line.size());
					size_t to = std::min(rangeEnd, line.size());
					if (to <= cursor)
						continue;
					appendPreserved(line.substr(cursor, from > cursor ? from - cursor : 0));
					appendMasked();
					cursor = to;
				}
				appendPreserved(line.substr(cursor));
			} else {
				appendPreserved(line);
			}
			start = end;
			lineIndex++;
		}
		sources[source->uri] =
			Json{{"digest", digest(projection)}, {"content", contentDigest}, {"mask", std::move(maskDigest)}};
	}
	if (work)
		work->sourceMilliseconds += elapsedMilliseconds(sourceStart);
	return sources;
}

bool sourceProjectionsEqual(const Json &left, const Json &right) {
	if (left.size() != right.size())
		return false;
	for (const auto &[uri, entry] : left.items())
		if (!right.contains(uri) || entry.at("digest") != right.at(uri).at("digest"))
			return false;
	return true;
}
