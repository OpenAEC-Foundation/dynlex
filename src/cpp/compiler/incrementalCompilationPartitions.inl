bool emitPartitions(
	ParseContext &context, const fs::path &directory, const std::string &compilerIdentity, const Json &options,
	const std::vector<std::string> &libraries, BuildStatistics &statistics
) {
	TemporaryObjects temporary;
	std::vector<std::string> objects;
	std::string prefix = "__dynlex_" + digest(fs::absolute(context.options.inputPath).lexically_normal().string()) + "_";
	for (llvm::GlobalValue &value : context.llvmModule->global_values()) {
		if (value.hasLocalLinkage())
			value.setName(prefix + value.getName().str());
	}
	// Cached functions enter the module through the LLVM linker, so their insertion
	// order differs from freshly generated functions. Keep partition IR stable.
	auto byName = [](const auto &left, const auto &right) {
		return left.getName() < right.getName();
	};
	std::vector<llvm::GlobalVariable *> globals;
	for (llvm::GlobalVariable &global : context.llvmModule->globals())
		globals.push_back(&global);
	std::sort(globals.begin(), globals.end(), [&](const auto *left, const auto *right) {
		return byName(*left, *right);
	});
	for (llvm::GlobalVariable *global : globals)
		context.llvmModule->removeGlobalVariable(global);
	for (llvm::GlobalVariable *global : globals)
		context.llvmModule->insertGlobalVariable(global);
	context.llvmModule->getFunctionList().sort(byName);
	bool success = true;
	auto partitionStart = Clock::now();
	llvm::SplitModule(*context.llvmModule, partitionCount, [&](std::unique_ptr<llvm::Module> part) {
		if (!success)
			return;
		bool hasDefinition =
			std::any_of(part->global_values().begin(), part->global_values().end(), [](const llvm::GlobalValue &value) {
			return !value.isDeclaration();
		});
		if (!hasDefinition && part->getModuleInlineAsm().empty())
			return;
		llvm::ModuleAnalysisManager analyses;
		llvm::StripDeadPrototypesPass().run(*part, analyses);
		std::string ir = renderModule(*part);
		std::string key = digest(compilerIdentity + options.dump() + ir);
		statistics.objectKeys.push_back(key);
		fs::path entry = directory / ("object-" + key + ".entry");
		fs::path object = temporary.directory / (std::to_string(objects.size()) + ".o");
		if (std::optional<std::string> cached = readEntry(entry)) {
			std::ofstream output(object, std::ios::binary);
			output.write(cached->data(), cached->size());
			output.close();
			if (!output)
				throw std::runtime_error("cannot materialize cached object: " + object.string());
			statistics.reusedObjects++;
		} else {
			if (!emitNativeObjectModule(context, *part, object.string())) {
				success = false;
				return;
			}
			writeEntry(entry, readFile(object));
			statistics.builtObjects++;
		}
		objects.push_back(object.string());
	});
	statistics.partitionMilliseconds = elapsedMilliseconds(partitionStart);
	auto linkStart = Clock::now();
	bool linked = success && emitNativeExecutableWithObjects(context, objects, libraries);
	statistics.linkMilliseconds = elapsedMilliseconds(linkStart);
	fs::remove_all(temporary.directory);
	temporary.directory.clear();
	return linked;
}
