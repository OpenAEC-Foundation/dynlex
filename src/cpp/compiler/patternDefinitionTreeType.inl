static SectionType definitionPatternTreeType(const Section *section) {
	requireCompilerInvariant(section != nullptr, "pattern definition has no owning section");
	if (section->isConversion)
		return SectionType::Conversion;
	if (section->type == SectionType::Class)
		return SectionType::Function;
	return section->type;
}
