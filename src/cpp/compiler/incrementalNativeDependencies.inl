struct NativePartitionDependencies {
	NativePartitionLayout layout;
	Json snapshot;
	std::array<bool, partitionCount> bodiesUnchanged;
	const Json *previous;

	NativePartitionDependencies(
		llvm::Module &module, const Json *previous, const std::unordered_set<std::string> &reusedFunctions,
		const std::map<std::string, const OptimizedFunctionOrigin *> &optimizedOrigins,
		const std::map<std::string, std::string> &renames, BuildStatistics &statistics
	)
		: layout(module), previous(previous) {
		bodiesUnchanged.fill(true);
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
			{"environment", digest(renderModule(*declarations))}, {"symbols", Json::object()}, {"partitions", Json::array()}
		};
		std::array<std::set<std::string>, partitionCount> owned, dependencies;
		for (const llvm::GlobalValue &value : module.global_values()) {
			if (value.isDeclaration())
				continue;
			std::string name = value.getName().str();
			unsigned partition = layout.partition(value);
			owned[partition].insert(name);
			const Json *old = previous && previous->at("symbols").contains(name) ? &previous->at("symbols").at(name) : nullptr;
			Json definition = nullptr;
			Json optimizedInput = nullptr;
			bool bodyUnchanged;
			std::set<std::string> references;
			if (llvm::isa<llvm::Function>(value)) {
				statistics.nativeFunctions++;
				auto origin = optimizedOrigins.find(name);
				if (origin != optimizedOrigins.end()) {
					Json bindings = Json::object();
					for (const auto &[canonical, original] : origin->second->bindings) {
						auto current = original ? renames.find(*original) : renames.end();
						bindings[canonical] = current == renames.end() ? Json(nullptr) : Json(current->second);
					}
					optimizedInput = digest(origin->second->inputKey + bindings.dump());
					statistics.nativeOptimizedBindings += origin->second->bindings.size();
				}
				if (old && !optimizedInput.is_null() && optimizedInput == old->at("optimized_input")) {
					definition = old->at("definition");
					bodyUnchanged = true;
					statistics.nativeOptimizedFunctionProofs++;
				} else if (old && reusedFunctions.contains(name)) {
					definition = old->at("definition");
					if (optimizedInput.is_null())
						optimizedInput = old->at("optimized_input");
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
			bodiesUnchanged[partition] = bodiesUnchanged[partition] && bodyUnchanged;
			dependencies[partition].insert(references.begin(), references.end());
			dependencies[partition].insert(name);
			snapshot["symbols"][name] = Json{
				{"definition", std::move(definition)}, {"references", references}, {"optimized_input", std::move(optimizedInput)}
			};
		}
		for (unsigned index = 0; index < partitionCount; index++) {
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
			snapshot["partitions"].push_back(
				Json{
					{"symbols", owned[index]},
					{"declarations", std::move(interfaces)},
					{"assembly", index == 0 ? module.getModuleInlineAsm() : ""},
					{"object_key", nullptr}
				}
			);
		}
	}

	bool hasDefinition(unsigned index) const {
		const Json &part = snapshot.at("partitions").at(index);
		return !part.at("symbols").empty() || !part.at("assembly").get_ref<const std::string &>().empty();
	}
	bool canReuse(unsigned index) const {
		if (!previous || !bodiesUnchanged[index] || previous->at("environment") != snapshot.at("environment"))
			return false;
		const Json &before = previous->at("partitions").at(index);
		const Json &after = snapshot.at("partitions").at(index);
		return before.at("object_key").is_string() && before.at("symbols") == after.at("symbols") &&
			   before.at("declarations") == after.at("declarations") && before.at("assembly") == after.at("assembly");
	}
};
