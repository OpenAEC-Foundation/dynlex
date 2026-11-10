struct TemporaryObjects {
	fs::path directory;
	TemporaryObjects() {
		llvm::SmallString<256> path;
		if (std::error_code error = llvm::sys::fs::createUniqueDirectory("dynlex-incremental", path))
			throw fs::filesystem_error("cannot create incremental build directory", error);
		directory = path.str().str();
	}
	~TemporaryObjects() {
		if (directory.empty())
			return;
		std::error_code error;
		fs::remove_all(directory, error);
		if (error)
			std::cerr << "Failed to clean incremental build directory: " << error.message() << '\n';
	}
};

struct BuildStatistics {
	std::string cacheDirectory;
	std::string cacheEntry;
	bool reusedFrontend = false;
	bool reusedTypeInference = false;
	bool linkedCachedObjects = false;
	size_t reusedInferenceInstantiations = 0;
	size_t reusedInferenceSummarySections = 0;
	size_t preinferredInstantiations = 0;
	size_t precedenceSignaturesReused = 0;
	size_t reusedFunctionIR = 0;
	size_t topLevelProjectionSectionsReused = 0;
	size_t reusedObjects = 0;
	size_t builtObjects = 0;
	size_t nativePartitionModulesBuilt = 0;
	size_t nativePartitionModulesSkipped = 0;
	size_t nativeFunctionDependenciesReused = 0;
	size_t nativeFunctionFingerprintsComputed = 0;
	Json nativeUnits;
	size_t dependencyGraphSectionsReused = 0;
	size_t preinferDependencyGraphSectionsChecked = 0;
	size_t dependencyGraphEntriesReused = 0;
	size_t dependencyGraphReverseNodesUpdated = 0;
	size_t inactiveProjectionMatchesRechecked = 0;
	size_t reusedDiagnosticRecords = 0;
	size_t matcherCacheHits = 0;
	size_t deferredMatchKeysReplayed = 0;
	size_t precedenceMatchKeysReplayed = 0;
	size_t precedenceMatcherCacheHits = 0;
	size_t expressionParseHits = 0;
	size_t temporaryExpressionParseHits = 0;
	size_t expressionParseMisses = 0;
	size_t definitionParseHits = 0;
	size_t definitionParseMisses = 0;
	SourceProjectionWork sourceProjectionWork;
	double matcherKeyMilliseconds = 0;
	double matcherRestoreMilliseconds = 0;
	double precedenceTreeSignatureMilliseconds = 0;
	double precedenceSignatureMilliseconds = 0;
	double precedenceRuleSignatureMilliseconds = 0;
	double cacheLoadMilliseconds = 0;
	double cachePayloadLoadMilliseconds = 0;
	size_t cachePayloadsLoaded = 0;
	double cacheRestoreMilliseconds = 0;
	double cacheFinalizeMilliseconds = 0;
	double totalMilliseconds = 0;
	double frontendMilliseconds = 0;
	double inactiveBodySelectionMilliseconds = 0;
	double matcherProjectionMilliseconds = 0;
	double inferenceMilliseconds = 0;
	double projectionMilliseconds = 0;
	double inactiveProjectionMilliseconds = 0;
	double parserSnapshotMilliseconds = 0;
	double matcherSnapshotMilliseconds = 0;
	double dependencyGraphMilliseconds = 0;
	double preinferenceMilliseconds = 0;
	double codegenMilliseconds = 0;
	double irGenerationMilliseconds = 0;
	double unoptimizedIRSnapshotMilliseconds = 0;
	double optimizationMilliseconds = 0;
	double snapshotMilliseconds = 0;
	double cacheWriteMilliseconds = 0;
	double partitionMilliseconds = 0;
	double nativeDependencyMilliseconds = 0;
	double linkMilliseconds = 0;
	std::vector<std::string> objectKeys;
};

std::optional<bool> linkCachedObjects(
	ParseContext &context, const fs::path &directory, const std::vector<std::string> &keys,
	const std::vector<std::string> &libraries, BuildStatistics &statistics
) {
	TemporaryObjects temporary;
	std::vector<std::string> paths;
	paths.reserve(keys.size());
	for (size_t index = 0; index < keys.size(); index++) {
		std::optional<std::string> object = readEntry(directory / ("object-" + keys[index] + ".entry"));
		if (!object)
			return std::nullopt;
		fs::path path = temporary.directory / (std::to_string(index) + ".o");
		std::ofstream output(path, std::ios::binary);
		output.write(object->data(), object->size());
		output.close();
		if (!output)
			throw std::runtime_error("cannot materialize cached object: " + path.string());
		paths.push_back(path.string());
	}
	statistics.reusedObjects = keys.size();
	statistics.linkedCachedObjects = true;
	auto linkStart = Clock::now();
	bool linked = emitNativeExecutableWithObjects(context, paths, libraries);
	statistics.linkMilliseconds = elapsedMilliseconds(linkStart);
	return linked;
}

void restoreCachedModule(ParseContext &context, const Json &cached, const fs::path &entry) {
	delete context.llvmModule;
	context.llvmModule = nullptr;
	std::unique_ptr<llvm::Module> module =
		parseCachedModule(cached.at("ir").get_ref<const std::string &>(), *context.llvmContext);
	if (llvm::verifyModule(*module))
		throw std::runtime_error("invalid cached LLVM IR: " + entry.string());
	module->setModuleIdentifier(cached.at("module_id").get<std::string>());
	context.llvmModule = module.release();
}
