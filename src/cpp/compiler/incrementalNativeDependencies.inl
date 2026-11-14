struct NativePartitionDependencies {
	NativePartitionLayout layout;
	Json snapshot;
	std::map<std::string, bool> bodiesUnchanged;
	const Json *previous;

	NativePartitionDependencies(
		llvm::Module &module, const Json *previous, const std::unordered_set<std::string> &reusedFunctions,
		BuildStatistics &statistics
	)
		: layout(module), previous(previous) {
		llvm::ValueToValueMapTy values;
		auto declarations = llvm::CloneModule(module, values, [](const llvm::GlobalValue *) {
			return false;
		});
		std::map<std::string, std::string> declarationHashes;
		std::map<std::string, std::set<std::string>> declarationReferences;
		for (const llvm::GlobalValue &value : module.global_values()) {
			std::string name = value.getName().str();
			const llvm::GlobalValue &declaration = *declarations->getNamedValue(name);
			std::string hash = nativeSymbolFingerprint(declaration, false);
			if (const llvm::Comdat *comdat = value.getComdat())
				hash = digest(hash + comdat->getName().str() + std::to_string(comdat->getSelectionKind()));
			declarationHashes.emplace(name, std::move(hash));
			declarationReferences.emplace(name, NativeSymbolReferences{}.collect(declaration));
		}
		std::vector<llvm::GlobalValue *> unused;
		for (llvm::GlobalValue &value : declarations->global_values())
			if (value.use_empty() && !value.isUsedByMetadata())
				unused.push_back(&value);
		for (llvm::GlobalValue *value : unused)
			value->eraseFromParent();
		declarations->setModuleInlineAsm("");
		snapshot = Json{
			{"environment", digest(renderModule(*declarations))}, {"symbols", Json::object()}, {"partitions", Json::object()}
		};
		std::map<std::string, std::set<std::string>> owned, dependencies;
		for (const llvm::GlobalValue &value : module.global_values()) {
			if (value.isDeclaration())
				continue;
			std::string name = value.getName().str();
			const std::string &partition = layout.component(value);
			owned[partition].insert(name);
			const Json *old = previous && previous->at("symbols").contains(name) ? &previous->at("symbols").at(name) : nullptr;
			Json definition = nullptr;
			bool bodyUnchanged;
			std::set<std::string> references;
			if (llvm::isa<llvm::Function>(value)) {
				statistics.nativeFunctions++;
				if (old && reusedFunctions.contains(name)) {
					definition = old->at("definition");
					bodyUnchanged = true;
				} else {
					definition = nativeSymbolFingerprint(value, true);
					bodyUnchanged = old && definition == old->at("definition");
					statistics.nativeFunctionFingerprintsComputed++;
				}
				if (bodyUnchanged) {
					references = old->at("references").get<std::set<std::string>>();
					statistics.nativeFunctionDependenciesReused++;
				} else
					references = NativeSymbolReferences{}.collect(value);
			} else {
				definition = nativeSymbolFingerprint(value, true);
				bodyUnchanged = old && definition == old->at("definition");
				references = NativeSymbolReferences{}.collect(value);
			}
			auto [unchanged, inserted] = bodiesUnchanged.try_emplace(partition, true);
			unchanged->second = unchanged->second && bodyUnchanged;
			dependencies[partition].insert(references.begin(), references.end());
			dependencies[partition].insert(name);
			snapshot["symbols"][name] = Json{{"definition", std::move(definition)}, {"references", references}};
		}
		// An empty component is reserved for module assembly; named definitions
		// have nonempty names and remain in their own LLVM-required components.
		if (!module.getModuleInlineAsm().empty()) {
			owned.try_emplace("");
			bodiesUnchanged.emplace("", true);
		}
		for (const auto &[index, names] : owned) {
			std::vector<std::string> pending(dependencies[index].begin(), dependencies[index].end());
			while (!pending.empty()) {
				std::string name = std::move(pending.back());
				pending.pop_back();
				for (const std::string &dependency : declarationReferences.at(name))
					if (dependencies[index].insert(dependency).second)
						pending.push_back(dependency);
			}
			Json interfaces = Json::object();
			for (const std::string &name : dependencies[index])
				interfaces[name] = declarationHashes.at(name);
			snapshot["partitions"][index] = Json{
				{"symbols", names},
				{"declarations", std::move(interfaces)},
				{"assembly", index.empty() ? module.getModuleInlineAsm() : ""},
				{"object_key", nullptr}
			};
		}
	}

	bool canReuse(const std::string &index) const {
		if (!previous || !bodiesUnchanged.at(index) || !previous->at("partitions").contains(index) ||
			previous->at("environment") != snapshot.at("environment"))
			return false;
		const Json &before = previous->at("partitions").at(index);
		const Json &after = snapshot.at("partitions").at(index);
		return before.at("object_key").is_string() && before.at("symbols") == after.at("symbols") &&
			   before.at("declarations") == after.at("declarations") && before.at("assembly") == after.at("assembly");
	}
};
