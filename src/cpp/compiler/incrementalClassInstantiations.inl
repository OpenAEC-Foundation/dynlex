Json classInstantiationSource(const ClassDefinition &definition) {
	Json fields = Json::array();
	for (const FieldDefinition &field : definition.fields) {
		requireCompilerInvariant(field.range.line && field.range.line->sourceFile, "class field has no source");
		fields.push_back(
			Json::array({field.name, field.range.line->sourceFile->uri, std::string(field.range.subString), field.alignment})
		);
	}
	return Json{{"patterns", definition.patternNames}, {"alignment", definition.alignment}, {"fields", std::move(fields)}};
}

Json captureClassDefinitionInstances(const ClassDefinition &definition, const InferenceValueIdentities &identities) {
	Json instances = Json::array();
	for (const ClassInstantiation &instance : definition.instantiations) {
		requireCompilerInvariant(
			instance.fieldTypes.size() == definition.fields.size(), "committed class instance has incomplete fields"
		);
		Json fields = Json::array();
		for (const DataType &field : instance.fieldTypes) {
			auto encoded = encodePortableConstraintType(field, identities.classes);
			requireCompilerInvariant(encoded.has_value(), "committed class field has no portable type identity");
			fields.push_back(std::move(*encoded));
		}
		instances.push_back(std::move(fields));
	}
	return Json{
		{"source", classInstantiationSource(definition)},
		{"instances", std::move(instances)},
		{"requests", encodeClassRequests(definition, identities)}
	};
}

Json captureClassInstantiations(const std::map<const Section *, std::string> &sectionPaths) {
	InferenceValueIdentities identities(sectionPaths);
	Json result = Json::object();
	for (const auto &[section, path] : sectionPaths) {
		if (section->type != SectionType::Class)
			continue;
		result[path] =
			captureClassDefinitionInstances(*static_cast<const ClassSection *>(section)->classDefinition, identities);
	}
	return result;
}

bool restoreClassInstantiations(const Json &saved, const std::map<std::string, Section *> &sections) {
	std::map<std::string, ClassDefinition *> classes;
	for (const auto &[path, entry] : saved.items()) {
		auto found = sections.find(path);
		if (found == sections.end() || found->second->type != SectionType::Class)
			return false;
		ClassDefinition *definition = static_cast<ClassSection *>(found->second)->classDefinition;
		if (classInstantiationSource(*definition) != entry.at("source") || entry.at("requests").is_null())
			return false;
		requireCompilerInvariant(
			definition->instantiations.empty() && definition->instantiationRequests.empty(),
			"class instances exist before incremental constraint restoration"
		);
		classes.emplace(path, definition);
	}
	for (const auto &[path, definition] : classes)
		definition->instantiations.resize(saved.at(path).at("instances").size());
	for (const auto &[path, definition] : classes) {
		const Json &instances = saved.at(path).at("instances");
		for (size_t index = 0; index < instances.size(); index++) {
			std::vector<DataType> fields;
			for (const Json &encoded : instances.at(index))
				fields.push_back(decodePortableConstraintType(encoded, sections));
			requireCompilerInvariant(fields.size() == definition->fields.size(), "saved class instance has incomplete fields");
			definition->instantiations[index].fieldTypes = std::move(fields);
		}
	}
	std::map<const Section *, std::string> paths;
	for (const auto &[path, section] : sections)
		paths.emplace(section, path);
	InferenceValueIdentities identities(paths);
	std::map<ClassDefinition *, std::map<std::string, std::shared_ptr<const ClassInstantiationRequest>>> requests;
	for (const auto &[path, definition] : classes) {
		auto decoded = decodeClassRequests(saved.at(path).at("requests"), identities);
		if (!decoded) {
			for (const auto &[_, value] : classes)
				value->instantiations.clear();
			return false;
		}
		requests.emplace(definition, std::move(*decoded));
	}
	for (auto &[definition, records] : requests)
		definition->instantiationRequests = std::move(records);
	return true;
}
