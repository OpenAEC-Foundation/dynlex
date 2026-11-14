struct OptimizationInput {
	std::string name;
	std::string bitcode;
};

void removeUnusedDeclarations(llvm::Module &module) {
	std::vector<llvm::GlobalValue *> unused;
	for (llvm::GlobalValue &value : module.global_values())
		if (value.isDeclaration() && value.use_empty() && !value.isUsedByMetadata())
			unused.push_back(&value);
	for (llvm::GlobalValue *value : unused)
		value->eraseFromParent();
}

std::vector<OptimizationInput> captureOptimizationInputs(
	ParseContext &context, const fs::path &directory, const Json *previous, BuildStatistics &statistics,
	const IncrementalIRReuse *functionReuse, std::set<std::string> &preserved
) {
	auto interfaceStart = Clock::now();
	llvm::Module &module = *context.llvmModule;
	std::string prefix = "__dynlex_" + digest(fs::absolute(context.options.inputPath).lexically_normal().string()) + "_";
	std::unordered_set<std::string> reusedFunctions;
	Json normalizedNames = Json::object();
	for (llvm::GlobalValue &value : module.global_values()) {
		if (!value.isDeclaration() && !value.hasLocalLinkage() && !value.isDiscardableIfUnused())
			preserved.insert(value.getName().str());
		bool reused = functionReuse && llvm::isa<llvm::Function>(value) &&
					  functionReuse->reusedFunctionNames().contains(value.getName().str());
		if (value.hasLocalLinkage()) {
			std::string original = value.getName().str();
			value.setName(prefix + value.getName().str());
			normalizedNames[original] = value.getName().str();
			value.setLinkage(llvm::GlobalValue::ExternalLinkage);
			value.setVisibility(llvm::GlobalValue::HiddenVisibility);
		}
		if (!value.hasName())
			value.setName(prefix + "anonymous");
		if (reused)
			reusedFunctions.insert(value.getName().str());
	}
	if (previous)
		for (const auto &[name, normalized] : normalizedNames.items())
			if (previous->at("names").contains(name) && previous->at("names").at(name) != normalized) {
				reusedFunctions.clear();
				break;
			}
	sortModuleSymbols(module);
	NativePartitionLayout layout(module);
	llvm::ValueToValueMapTy values;
	auto declarations = llvm::CloneModule(module, values, [](const llvm::GlobalValue *) {
		return false;
	});
	std::map<std::string, std::string> interfaces;
	std::map<std::string, std::set<std::string>> interfaceReferences;
	for (const llvm::GlobalValue &value : module.global_values()) {
		std::string name = value.getName().str();
		const llvm::GlobalValue &declaration = *declarations->getNamedValue(name);
		std::string fingerprint = nativeSymbolFingerprint(declaration, false);
		if (const llvm::Comdat *comdat = value.getComdat())
			fingerprint = digest(fingerprint + comdat->getName().str() + std::to_string(comdat->getSelectionKind()));
		interfaces.emplace(name, std::move(fingerprint));
		interfaceReferences.emplace(name, NativeSymbolReferences{}.collect(declaration));
	}
	removeUnusedDeclarations(*declarations);
	declarations->setModuleInlineAsm("");
	std::string environment = digest(renderModule(*declarations));
	Json snapshot{
		{"environment", environment},
		{"names", std::move(normalizedNames)},
		{"symbols", Json::object()},
		{"units", Json::object()}
	};
	statistics.optimizationInterfaceMilliseconds += elapsedMilliseconds(interfaceStart);
	auto symbolStart = Clock::now();
	std::map<std::string, std::set<std::string>> owned, dependencies;
	for (const llvm::GlobalValue &value : module.global_values()) {
		if (value.isDeclaration())
			continue;
		std::string name = value.getName().str();
		const std::string &unit = layout.component(value);
		owned[unit].insert(name);
		const Json *old = previous && previous->at("symbols").contains(name) ? &previous->at("symbols").at(name) : nullptr;
		Json symbol;
		if (old && reusedFunctions.contains(name))
			symbol = *old;
		else
			symbol = Json{
				{"definition", nativeSymbolFingerprint(value, true)}, {"references", NativeSymbolReferences{}.collect(value)}
			};
		std::set<std::string> references = symbol.at("references").get<std::set<std::string>>();
		dependencies[unit].insert(references.begin(), references.end());
		dependencies[unit].insert(name);
		snapshot["symbols"][name] = std::move(symbol);
	}
	statistics.optimizationSymbolMilliseconds += elapsedMilliseconds(symbolStart);
	std::vector<OptimizationInput> inputs;
	for (const auto &[unit, names] : owned) {
		auto checkStart = Clock::now();
		std::vector<std::string> pending(dependencies[unit].begin(), dependencies[unit].end());
		while (!pending.empty()) {
			std::string name = std::move(pending.back());
			pending.pop_back();
			for (const std::string &dependency : interfaceReferences.at(name))
				if (dependencies[unit].insert(dependency).second)
					pending.push_back(dependency);
		}
		Json definitions = Json::object(), declarationsUsed = Json::object();
		for (const std::string &name : names)
			definitions[name] = snapshot.at("symbols").at(name).at("definition");
		for (const std::string &name : dependencies[unit])
			declarationsUsed[name] = interfaces.at(name);
		std::string assembly = inputs.empty() ? module.getModuleInlineAsm() : "";
		Json current{
			{"definitions", std::move(definitions)}, {"declarations", std::move(declarationsUsed)}, {"assembly", assembly}
		};
		std::string key;
		std::optional<std::string> bitcode;
		if (previous && previous->at("environment") == environment && previous->at("units").contains(unit)) {
			Json old = previous->at("units").at(unit);
			key = old.at("input_key").get<std::string>();
			old.erase("input_key");
			if (old == current)
				bitcode = readEntry(directory / ("optimization-input-" + key + ".entry"));
			if (bitcode && digest(*bitcode) != key)
				throw std::runtime_error("corrupt optimization input: " + key);
		}
		std::string identifier = prefix + "unit-" + digest(unit);
		statistics.optimizationInputCheckMilliseconds += elapsedMilliseconds(checkStart);
		if (bitcode)
			statistics.optimizationInputUnitsReused++;
		else {
			auto buildStart = Clock::now();
			std::vector<const llvm::GlobalValue *> definitions;
			for (const std::string &name : names)
				definitions.push_back(module.getNamedValue(name));
			auto part = cloneModulePartition(module, definitions, statistics.optimizationInputCloneWork);
			part->setModuleIdentifier(identifier);
			part->setModuleInlineAsm(assembly);
			removeUnusedDeclarations(*part);
			// A link-once definition can be unused in this partition while other
			// partitions still reference it. Keep it until the combined index has
			// decided liveness; combination restores its original linkage.
			for (llvm::GlobalValue &value : part->global_values())
				if (!value.isDeclaration() && value.hasLinkOnceLinkage())
					value.setLinkage(
						value.hasLinkOnceODRLinkage() ? llvm::GlobalValue::WeakODRLinkage : llvm::GlobalValue::WeakAnyLinkage
					);
			optimizeLLVMModule(context, *part, true);
			sortModuleSymbols(*part);
			llvm::ProfileSummaryInfo profile(*part);
			auto summary = llvm::buildModuleSummaryIndex(*part, {}, &profile);
			std::string bytes;
			llvm::raw_string_ostream stream(bytes);
			llvm::WriteBitcodeToFile(*part, stream, false, &summary, true);
			stream.flush();
			key = digest(bytes);
			writeEntry(directory / ("optimization-input-" + key + ".entry"), bytes);
			bitcode = std::move(bytes);
			statistics.optimizationInputUnitsBuilt++;
			statistics.optimizationInputBuildMilliseconds += elapsedMilliseconds(buildStart);
		}
		current["input_key"] = key;
		snapshot["units"][unit] = std::move(current);
		inputs.push_back({std::move(identifier), std::move(*bitcode)});
	}
	statistics.optimizationUnits = std::move(snapshot);
	return inputs;
}
