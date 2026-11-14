bool classes(const Json &references, const std::set<std::string> &required) {
	auto records = owner.summaryStore.loadClasses(references, required);
	if (!records)
		return false;
	std::map<std::string, ClassDefinition *> definitions;
	for (const auto &[path, record] : records->items()) {
		auto found = owner.identities.sections.find(path);
		if (found == owner.identities.sections.end() || found->second->type != SectionType::Class)
			return false;
		auto *definition = static_cast<ClassSection *>(found->second)->classDefinition;
		if (classInstantiationSource(*definition) != record.at("source") || record.at("requests").is_null())
			return false;
		const auto &saved = record.at("instances");
		for (size_t index = 0; index < std::min(definition->instantiations.size(), saved.size()); index++) {
			Json fields = Json::array();
			for (const auto &field : definition->instantiations[index].fieldTypes) {
				auto type = encodePortableConstraintType(field, owner.identities.classes);
				requireCompilerInvariant(type.has_value(), "committed class field has no portable type");
				fields.push_back(std::move(*type));
			}
			if (fields != saved.at(index))
				return false;
		}
		definitions.emplace(path, definition);
	}
	// Reserve the complete class dependency cycle before decoding any fields.
	std::map<ClassDefinition *, size_t> appended;
	for (const auto &[path, definition] : definitions) {
		size_t size = records->at(path).at("instances").size();
		if (size > definition->instantiations.size()) {
			recordInferenceClassRestoration(caller, definition);
			classSizes.try_emplace(definition, definition->instantiations.size());
			appended.emplace(definition, definition->instantiations.size());
			definition->instantiations.resize(size);
		}
	}
	for (const auto &[path, definition] : definitions) {
		auto original = appended.find(definition);
		if (original == appended.end())
			continue;
		const Json &saved = records->at(path).at("instances");
		bool restored = false;
		for (size_t index = original->second; index < saved.size(); index++) {
			std::vector<DataType> fields;
			for (const Json &field : saved.at(index))
				fields.push_back(decodePortableConstraintType(field, owner.identities.classes));
			requireCompilerInvariant(fields.size() == definition->fields.size(), "cached class has an incomplete layout");
			definition->instantiations[index].fieldTypes = std::move(fields);
			restored = true;
		}
		if (restored)
			owner.summaryStore.recordClassRestored();
	}
	for (const auto &[path, definition] : definitions) {
		auto requests = decodeClassRequests(records->at(path).at("requests"), owner.identities);
		if (!requests)
			return false;
		for (auto &[key, request] : *requests) {
			auto found = definition->instantiationRequests.find(key);
			if (found != definition->instantiationRequests.end()) {
				if (found->second->index != request->index)
					return false;
				continue;
			}
			recordInferenceClassRequestRestoration(caller, definition, key);
			classRequests.emplace_back(definition, key);
			definition->instantiationRequests.emplace(key, std::move(request));
		}
	}
	return true;
}

bool types(const Json &summary, const Json &reference) {
	std::set<std::string> missing;
	auto require = [&](const Json &path, int index) {
		if (index < 0 || path.is_null())
			return;
		auto found = owner.identities.sections.find(path.get<std::string>());
		if (found == owner.identities.sections.end() || found->second->type != SectionType::Class ||
			index >= static_cast<int>(static_cast<ClassSection *>(found->second)->classDefinition->instantiations.size()))
			missing.insert(path.get<std::string>());
	};
	std::function<void(const Json &)> portable = [&](const Json &type) {
		require(type.at(7), type.at(8).get<int>());
		if (!type.at(6).is_null())
			portable(type.at(6));
		if (!type.at(9).is_null()) {
			portable(type.at(9).at(0));
			for (const auto &parameter : type.at(9).at(1))
				portable(parameter);
		}
	};
	std::function<void(const Json &)> visit = [&](const Json &type) {
		switch (static_cast<DataType::Kind>(type.at(0).get<int>())) {
		case DataType::Kind::Class:
			require(type.at(3), type.at(4).get<int>());
			if (type.size() == 6)
				for (const Json &field : type.at(5))
					visit(field);
			break;
		case DataType::Kind::Type:
			portable(type.at(3));
			if (!type.at(4).is_null())
				visit(type.at(4));
			break;
		case DataType::Kind::Array:
		case DataType::Kind::Vector:
		case DataType::Kind::Matrix:
			visit(type.at(5));
			break;
		case DataType::Kind::Function:
			visit(type.at(3));
			for (const Json &parameter : type.at(4))
				visit(parameter);
			break;
		default:
			break;
		}
	};
	(void)remapInferenceSummaryTypes(summary, [&](const Json &type) {
		visit(type);
		return nullptr;
	});
	return missing.empty() || classes(reference.at("classes"), missing);
}
