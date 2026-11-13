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
	ValidationWork validation;
	VariableBindingWork bindings;
	size_t bindingInputsReused = 0;
	double bindingInputMilliseconds = 0, bindingLookupMilliseconds = 0, bindingEvaluationMilliseconds = 0,
		   bindingRecordMilliseconds = 0;
	std::string cacheDirectory;
	std::string cacheEntry;
	bool reusedFrontend = false;
	bool reusedTypeInference = false;
	bool linkedCachedObjects = false;
	size_t reusedInferenceInstantiations = 0;
	size_t restoredCurrentInferenceProducts = 0;
	size_t reusedInferenceSummarySections = 0;
	size_t preinferredInstantiations = 0;
	size_t preinferArgumentVariantsDecoded = 0;
	size_t preinferSummarySectionsCaptured = 0;
	size_t preinferSummaryInstantiationsCaptured = 0;
	size_t inferenceUnitInputsComputed = 0;
	size_t inferenceUnitsCaptured = 0;
	size_t inferenceUnitsReused = 0;
	size_t inferenceSummaryUnitsLoaded = 0;
	size_t valueDependencyQueriesChecked = 0;
	size_t valueDependencyQueriesReused = 0;
	size_t inferenceSummaryUnitsEncoded = 0;
	size_t inferenceSummaryUnitsWritten = 0;
	size_t inferenceBodiesEncoded = 0;
	size_t inferenceBodyTransfers = 0;
	size_t inferenceBodyValuesEncoded = 0, inferenceBodyValueReferences = 0;
	size_t inferenceBodyInstantiationsEncoded = 0, inferenceBodyInstantiationReferences = 0;
	size_t inferenceStorageIndicesBuilt = 0, inferenceStorageOriginsIndexed = 0;
	size_t inferenceBodiesWritten = 0;
	size_t inferenceClassesLoaded = 0;
	size_t inferenceClassesRestored = 0;
	size_t inferenceClassesWritten = 0;
	size_t inferenceClassRecordRequests = 0;
	double inferenceClassRecordMilliseconds = 0;
	size_t inferenceClassStatesCaptured = 0;
	size_t inferenceSourceExpressionLookups = 0;
	size_t inferenceSourceIndexBuilds = 0, inferenceSourceExpressionsIndexed = 0;
	size_t inferenceSourceExpressionsEncoded = 0;
	double inferenceSourceIndexMilliseconds = 0;
	double inferenceBodyEncodeMilliseconds = 0;
	double inferenceBodyCaptureMilliseconds = 0;
	double inferenceSummaryLoadMilliseconds = 0;
	double inferenceSummaryEncodeMilliseconds = 0;
	size_t aggregateInferenceUnitsCaptured = 0;
	size_t aggregateInferenceUnitsReused = 0;
	double inferenceUnitInputMilliseconds = 0;
	bool reusedRootExecution = false;
	size_t cachedIRDependencyScans = 0;
	size_t cachedIRDependencyHits = 0;
	size_t cachedIRGlobalInputChecks = 0;
	size_t cachedIRGlobalInputHits = 0;
	size_t precedenceSignaturesReused = 0;
	size_t reusedFunctionIR = 0;
	size_t topLevelProjectionSectionsReused = 0;
	size_t reusedObjects = 0;
	size_t builtObjects = 0;
	size_t nativePartitionModulesBuilt = 0;
	size_t nativePartitionModulesSkipped = 0;
	ModulePartitionCloneWork nativePartitionCloneWork;
	ModulePartitionCloneWork optimizationInputCloneWork;
	size_t nativeFunctionDependenciesReused = 0;
	size_t nativeFunctionFingerprintsComputed = 0;
	size_t nativeFunctions = 0;
	size_t nativeFunctionsLowered = 0;
	size_t nativeUnreachableUnits = 0;
	double nativeReachabilityMilliseconds = 0;
	double nativeUnitMilliseconds = 0;
	Json nativeUnits;
	Json optimizationUnits;
	size_t optimizationInputUnitsBuilt = 0;
	size_t optimizationInputUnitsReused = 0;
	size_t optimizationUnitsBuilt = 0;
	size_t optimizationUnitsReused = 0;
	double optimizationInputMilliseconds = 0;
	double optimizationInterfaceMilliseconds = 0;
	double optimizationSymbolMilliseconds = 0;
	double optimizationInputCheckMilliseconds = 0;
	double optimizationInputBuildMilliseconds = 0;
	double optimizationIndexMilliseconds = 0;
	double optimizationBackendMilliseconds = 0;
	size_t dependencyGraphSectionsReused = 0;
	size_t preinferDependencyGraphSectionsChecked = 0;
	size_t dependencyGraphEntriesReused = 0;
	size_t dependencyGraphReverseNodesUpdated = 0;
	size_t inactiveProjectionMatchesRechecked = 0;
	size_t reusedDiagnosticRecords = 0;
	size_t matcherCacheHits = 0;
	size_t fileObservationRequests = 0;
	size_t fileObservationsComputed = 0;
	size_t fileObservationsReused = 0;
	size_t referenceMetadataUnitsLoaded = 0;
	size_t referenceMetadataUnitsReused = 0;
	size_t matcherKeyComputations = 0;
	size_t matcherSignatureLookups = 0;
	size_t deferredMatchKeysReplayed = 0;
	size_t precedenceMatchKeysReplayed = 0;
	size_t precedenceMatcherCacheHits = 0;
	size_t expressionParseHits = 0;
	size_t temporaryExpressionParseHits = 0;
	size_t expressionParseMisses = 0;
	size_t definitionParseHits = 0;
	size_t definitionParseMisses = 0;
	SourceProjectionWork sourceProjectionWork;
	ClassificationWork classification;
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
	CacheTrimWork cacheTrimWork;
	double totalMilliseconds = 0;
	double frontendMilliseconds = 0;
	double cachedBodySelectionMilliseconds = 0;
	double parameterCountCacheMilliseconds = 0;
	double matcherProjectionMilliseconds = 0;
	size_t matcherDeclarationsReused = 0;
	size_t matcherPathSnapshotsBuilt = 0;
	size_t matcherPathSnapshotsLoaded = 0;
	size_t callableLookupsObserved = 0;
	size_t failedCallableLookupsObserved = 0;
	size_t callableLookupsChecked = 0;
	size_t callableLookupsChanged = 0;
	double inferenceMilliseconds = 0;
	double constraintPrepareMilliseconds = 0, constraintSnapshotMilliseconds = 0;
	double projectionMilliseconds = 0;
	double inactiveProjectionMilliseconds = 0;
	double parserSnapshotMilliseconds = 0;
	double matcherSnapshotMilliseconds = 0;
	size_t matcherSnapshotBytes = 0;
	IncrementalMatcherCache::StorageStatistics matcherStorage;
	size_t globalInputDefinitionsIndexed = 0, globalTypeInputsCaptured = 0, globalTypeInputsChecked = 0,
		   globalTypeInputFingerprintsComputed = 0, globalTypeInputReadsRecorded = 0;
	size_t deferredQueryProbes = 0, deferredQueryInterfacesPreserved = 0;
	double deferredQueryMilliseconds = 0;
	double matcherSerializeMilliseconds = 0;
	double matcherSerializeCPUMilliseconds = 0;
	double matcherDigestMilliseconds = 0;
	double matcherDigestCPUMilliseconds = 0;
	double matcherStoreMilliseconds = 0;
	double matcherStoreCPUMilliseconds = 0;
	double dependencyGraphMilliseconds = 0;
	double preinferenceMilliseconds = 0;
	double codegenMilliseconds = 0;
	double irGenerationMilliseconds = 0;
	double generatedIRSnapshotMilliseconds = 0;
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

void restoreGeneratedModule(ParseContext &context, const Json &cached, const fs::path &entry) {
	delete context.llvmModule;
	context.llvmModule = nullptr;
	std::unique_ptr<llvm::Module> module =
		parseCachedModule(cached.at("generated_ir").get_ref<const std::string &>(), *context.llvmContext);
	if (llvm::verifyModule(*module))
		throw std::runtime_error("invalid cached LLVM IR: " + entry.string());
	module->setModuleIdentifier(cached.at("module_id").get<std::string>());
	context.llvmModule = module.release();
}
