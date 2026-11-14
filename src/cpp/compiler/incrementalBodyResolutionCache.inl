// Body units share the compact matcher store. Their keys never enter the
// ordinary matcher: the leading kind byte distinguishes their serialized data.
std::optional<std::string> IncrementalMatcherCache::recordBodyResolution(
	const std::vector<std::pair<size_t, PatternReference *>> &references, bool initial
) {
	if (references.empty())
		return std::nullopt;
	std::string packed(1, '\2');
	appendLength(packed, references.size());
	for (const auto &[index, reference] : references) {
		requireCompilerInvariant(reference->resolved, "body-resolution snapshot contains an unresolved reference");
		if (!reference->variableResolutionOffsets &&
			(initial ? reference->initialMatcherKey : reference->incrementalMatcherKey).empty())
			return std::nullopt;
		appendLength(packed, index);
		appendString(packed, initial ? reference->initialMatcherKey : reference->incrementalMatcherKey);
		appendLength(packed, reference->variableResolutionOffsets.has_value());
		if (reference->variableResolutionOffsets) {
			requireCompilerInvariant(
				reference->variableResolutionOffsets->first >= 0 &&
					reference->variableResolutionOffsets->second >= reference->variableResolutionOffsets->first,
				"variable resolution has an invalid source span"
			);
			const PatternElement &element = reference->patternElements.front();
			appendString(packed, element.text);
			appendLength(packed, element.startPos);
			appendLength(packed, element.isArgumentPack);
			appendLength(packed, reference->variableResolutionOffsets->first);
			appendLength(packed, reference->variableResolutionOffsets->second);
		}
	}
	std::string key = digest(packed);
	recordValue(key, std::move(packed));
	return key;
}

bool IncrementalMatcherCache::visitBodyResolution(std::string_view key, const std::function<void(BodyResolution)> &visit) {
	auto entry = findPrevious(key);
	if (entry == previousEntries.end())
		return false;
	std::string_view packed = entry->second;
	if (packed.empty() || packed.front() != '\2')
		throw std::runtime_error("invalid body-resolution cache kind");
	size_t position = 1;
	std::uint64_t count = readLength(packed, position);
	for (std::uint64_t index = 0; index < count; index++) {
		size_t referenceIndex = readLength(packed, position);
		std::string matchKey(readString(packed, position));
		BodyResolution reference{referenceIndex, std::move(matchKey), {}};
		std::uint64_t variable = readLength(packed, position);
		if (variable > 1)
			throw std::runtime_error("invalid body-resolution cache outcome");
		if (variable) {
			std::string name(readString(packed, position));
			size_t startPos = readLength(packed, position);
			PatternElement element(PatternElement::Type::Variable, std::move(name), startPos);
			element.isArgumentPack = readLength(packed, position);
			int start = readLength(packed, position);
			int end = readLength(packed, position);
			reference.variable.emplace(std::move(element), start, end);
		}
		visit(std::move(reference));
	}
	if (position != packed.size())
		throw std::runtime_error("trailing body-resolution cache data");
	retain(key);
	return true;
}

std::optional<std::vector<IncrementalMatcherCache::BodyResolution>>
IncrementalMatcherCache::restoreBodyResolution(std::string_view key) {
	std::vector<BodyResolution> result;
	if (!visitBodyResolution(key, [&](BodyResolution reference) {
		result.push_back(std::move(reference));
	}))
		return std::nullopt;
	return result;
}

bool IncrementalMatcherCache::retainBodyResolutionDependencies(std::string_view key) {
	std::vector<std::string> dependencies;
	if (!visitBodyResolution(key, [&](BodyResolution reference) {
		if (!reference.matchKey.empty())
			dependencies.push_back(std::move(reference.matchKey));
	}))
		return false;
	if (!std::ranges::all_of(dependencies, [&](const std::string &dependency) {
		return contains(dependency);
	}))
		return false;
	for (const std::string &dependency : dependencies)
		retain(dependency);
	return true;
}
