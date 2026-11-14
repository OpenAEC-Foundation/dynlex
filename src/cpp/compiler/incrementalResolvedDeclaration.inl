Json resolvedDefinitionElements(const std::vector<DefinitionPatternElement> &elements) {
	DeclarationJsonWriter writer;
	writeResolvedDefinitionElements(writer, elements);
	return writer.finish();
}

Json sectionDeclaration(const Section &section) {
	DeclarationJsonWriter writer;
	writeSectionDeclaration(writer, section);
	return writer.finish();
}
