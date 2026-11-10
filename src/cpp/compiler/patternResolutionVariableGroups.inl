static void materializeVariableGroups(ParseContext &context) {
	std::vector<std::string> unresolvedNames;
	unresolvedNames.reserve(context.unresolvedVariableReferences.size());
	for (auto &[name, _] : context.unresolvedVariableReferences)
		unresolvedNames.push_back(name);
	std::sort(unresolvedNames.begin(), unresolvedNames.end());

	for (const std::string &name : unresolvedNames) {
		auto refsIt = context.unresolvedVariableReferences.find(name);
		if (refsIt == context.unresolvedVariableReferences.end())
			continue;
		auto &references = refsIt->second;
		bool isGlobal = context.declaredGlobalVariables.contains(name);

		std::unordered_map<VariableReference *, Section *> referenceOwners;
		std::unordered_map<Section *, VariableReference *> earliestImplicitSectionReferences;
		for (VariableReference *ref : references) {
			Section *sec = ref->range.section();
			if (Section *explicitSection = visibleExplicitVariableSection(sec, name, ref)) {
				referenceOwners[ref] = explicitSection;
				continue;
			}
			auto [existing, inserted] = earliestImplicitSectionReferences.emplace(sec, ref);
			if (!inserted && variableReferenceSourcePosition(ref) < variableReferenceSourcePosition(existing->second))
				existing->second = ref;
		}
		std::unordered_map<Section *, Section *> implicitSectionOwners;
		for (const auto &[sec, _] : earliestImplicitSectionReferences) {
			implicitSectionOwners[sec] = highestImplicitVariableSection(sec, name, earliestImplicitSectionReferences);
		}
		std::unordered_map<Section *, std::vector<VariableReference *>> groups;
		for (VariableReference *ref : references) {
			auto explicitOwner = referenceOwners.find(ref);
			Section *owner =
				explicitOwner != referenceOwners.end() ? explicitOwner->second : implicitSectionOwners.at(ref->range.section());
			groups[owner].push_back(ref);
		}
		std::vector<Section *> orderedGroupSections;
		orderedGroupSections.reserve(groups.size());
		for (auto &[highestSection, _] : groups)
			orderedGroupSections.push_back(highestSection);
		std::sort(orderedGroupSections.begin(), orderedGroupSections.end(), sectionComesBefore);

		for (Section *highestSection : orderedGroupSections) {
			auto groupIt = groups.find(highestSection);
			if (groupIt == groups.end())
				continue;
			materializeVariableGroup(name, highestSection, groupIt->second, isGlobal);
		}
	}
}
