void sortModuleSymbols(llvm::Module &module) {
	auto byName = [](const auto &left, const auto &right) {
		return left.getName() < right.getName();
	};
	std::vector<llvm::GlobalVariable *> globals;
	for (llvm::GlobalVariable &global : module.globals())
		globals.push_back(&global);
	std::sort(globals.begin(), globals.end(), [&](const auto *left, const auto *right) {
		return byName(*left, *right);
	});
	for (llvm::GlobalVariable *global : globals)
		module.removeGlobalVariable(global);
	for (llvm::GlobalVariable *global : globals)
		module.insertGlobalVariable(global);
	module.getFunctionList().sort(byName);
}

bool emitPartitions(
	ParseContext &context, const fs::path &directory, const std::string &compilerIdentity, const Json &options,
	const std::vector<std::string> &libraries, BuildStatistics &statistics, Json *previous = nullptr,
	const IncrementalIRReuse *functionReuse = nullptr, bool restoredModule = false
) {
	TemporaryObjects temporary;
	std::vector<std::string> objects;
	auto partitionStart = Clock::now();
	if (previous) {
		auto payloadStart = Clock::now();
		statistics.cachePayloadsLoaded += hydrateProgramField(*previous, directory, "native_units");
		statistics.cachePayloadLoadMilliseconds += elapsedMilliseconds(payloadStart);
	}
	std::unordered_set<std::string> reusedFunctions;
	std::string prefix = "__dynlex_" + digest(fs::absolute(context.options.inputPath).lexically_normal().string()) + "_";
	for (llvm::GlobalValue &value : context.llvmModule->global_values()) {
		bool reused =
			llvm::isa<llvm::Function>(value) && !value.isDeclaration() &&
			(restoredModule || (functionReuse && functionReuse->reusedFunctionNames().contains(value.getName().str())));
		if (value.hasLocalLinkage()) {
			value.setName(prefix + value.getName().str());
			value.setLinkage(llvm::GlobalValue::ExternalLinkage);
			value.setVisibility(llvm::GlobalValue::HiddenVisibility);
		}
		if (!value.hasName())
			value.setName(prefix + "anonymous");
		if (reused)
			reusedFunctions.insert(value.getName().str());
	}
	// Cached functions enter through the LLVM linker in a different insertion
	// order. Normalize it for stable partition keys, including restored modules.
	sortModuleSymbols(*context.llvmModule);
	auto dependencyStart = Clock::now();
	NativePartitionDependencies dependencies(
		*context.llvmModule, previous ? &previous->at("native_units") : nullptr, reusedFunctions, statistics
	);
	statistics.nativeDependencyMilliseconds += elapsedMilliseconds(dependencyStart);
	for (unsigned index = 0; index < partitionCount; index++) {
		if (!dependencies.hasDefinition(index))
			continue;
		bool unchanged = dependencies.canReuse(index);
		std::string key;
		std::optional<std::string> cached;
		if (unchanged) {
			key = previous->at("native_units").at("partitions").at(index).at("object_key").get<std::string>();
			cached = readEntry(directory / ("object-" + key + ".entry"));
		}
		std::unique_ptr<llvm::Module> part;
		if (!cached) {
			llvm::ValueToValueMapTy values;
			part = llvm::CloneModule(*context.llvmModule, values, [&](const llvm::GlobalValue *value) {
				return !value->isDeclaration() && dependencies.layout.partition(*value) == index;
			});
			if (index != 0)
				part->setModuleInlineAsm("");
			llvm::ModuleAnalysisManager analyses;
			llvm::StripDeadPrototypesPass().run(*part, analyses);
			statistics.nativePartitionModulesBuilt++;
			if (!unchanged)
				key = digest(compilerIdentity + options.dump() + renderModule(*part));
			cached = readEntry(directory / ("object-" + key + ".entry"));
		} else
			statistics.nativePartitionModulesSkipped++;
		dependencies.snapshot["partitions"][index]["object_key"] = key;
		statistics.objectKeys.push_back(key);
		fs::path object = temporary.directory / (std::to_string(objects.size()) + ".o");
		if (cached) {
			std::ofstream output(object, std::ios::binary);
			output.write(cached->data(), cached->size());
			output.close();
			if (!output)
				throw std::runtime_error("cannot materialize cached object: " + object.string());
			statistics.reusedObjects++;
		} else {
			if (!emitNativeObjectModule(context, *part, object.string()))
				return false;
			writeEntry(directory / ("object-" + key + ".entry"), readFile(object));
			statistics.builtObjects++;
		}
		objects.push_back(object.string());
	}
	statistics.nativeUnits = std::move(dependencies.snapshot);
	statistics.partitionMilliseconds = elapsedMilliseconds(partitionStart);
	if (previous && previous->at("object_keys") == statistics.objectKeys && previous->at("libraries") == libraries &&
		linkedOutputUnchanged(context, *previous)) {
		fs::remove_all(temporary.directory);
		temporary.directory.clear();
		return true;
	}
	auto linkStart = Clock::now();
	bool linked = emitNativeExecutableWithObjects(context, objects, libraries);
	statistics.linkMilliseconds = elapsedMilliseconds(linkStart);
	fs::remove_all(temporary.directory);
	temporary.directory.clear();
	return linked;
}
