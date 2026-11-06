Json sourceProjection(const ParseContext &context, const std::unordered_set<const Section *> &inactive) {
	using SourceRanges = std::map<const lsp::SourceFile *, std::map<int, std::vector<std::pair<size_t, size_t>>>>;
	std::map<const lsp::SourceFile *, std::set<int>> masked;
	std::map<const lsp::SourceFile *, std::set<int>> preserved;
	SourceRanges maskedRanges;
	for (const CodeLine *line : context.codeLines) {
		bool insideInactive = false;
		for (const Section *section = line->section; section; section = section->parent) {
			if (inactive.contains(section)) {
				insideInactive = true;
				break;
			}
		}
		(insideInactive ? masked : preserved)[line->sourceFile].insert(line->sourceFileLineIndex);
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
		}
	}
	Json sources = Json::object();
	for (const auto &[_, source] : context.importedFiles) {
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
			std::string_view line = content.substr(start, end - start);
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
		sources[source->uri] = digest(projection);
	}
	return sources;
}
