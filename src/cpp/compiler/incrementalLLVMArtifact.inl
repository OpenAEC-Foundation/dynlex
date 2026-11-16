// LLVM text has its own final artifact; semantic and function-IR reuse remain
// owned by the common frontend. Native object producers do not call this path.
void initializeOutputTarget(ParseContext &context) {
	requireCompilerInvariant(
		!context.llvmContext && !context.llvmModule && !context.targetMachine, "cached output target initialized twice"
	);
	context.llvmContext = new llvm::LLVMContext();
	context.llvmModule = new llvm::Module("dynlex_cached_output", *context.llvmContext);
	std::string error;
	context.targetMachine = createNativeTargetMachine(context, error);
	if (!context.targetMachine)
		throw std::runtime_error(error);
}

bool buildLLVMArtifact(
	ParseContext &context, const fs::path &directory, const std::string &compilerIdentity, const Json &options,
	std::string_view generatedIR, BuildStatistics &statistics
) {
	statistics.llvmOutputKey = digest(compilerIdentity + options.dump() + std::string(generatedIR));
	fs::path entry = directory / ("llvm-output-" + statistics.llvmOutputKey + ".entry");
	if (auto text = readEntry(entry)) {
		statistics.llvmOutputsRestored++;
		return writeLLVMOutput(context, *text);
	}
	auto start = Clock::now();
	if (context.options.optimizationLevel)
		optimizeLLVMModule(context, *context.llvmModule);
	statistics.optimizationMilliseconds += elapsedMilliseconds(start);
	std::string text = renderLLVMOutput(*context.llvmModule);
	if (!writeLLVMOutput(context, text))
		return false;
	writeEntry(entry, text);
	statistics.llvmOutputsBuilt++;
	return true;
}

bool restoreLLVMArtifact(
	ParseContext &context, const fs::path &directory, const fs::path &entry, const std::string &compilerIdentity,
	const Json &options, Json &previous, BuildStatistics &statistics, bool generatedModuleAvailable, bool initializeTarget
) {
	statistics.llvmOutputKey = previous.at("llvm_output_key").get<std::string>();
	if (auto text = readEntry(directory / ("llvm-output-" + statistics.llvmOutputKey + ".entry"))) {
		statistics.llvmOutputsRestored++;
		return writeLLVMOutput(context, *text);
	}
	if (initializeTarget)
		initializeOutputTarget(context);
	if (!generatedModuleAvailable) {
		auto start = Clock::now();
		statistics.cachePayloadsLoaded += hydrateProgramField(previous, directory, "generated_ir");
		statistics.cachePayloadLoadMilliseconds += elapsedMilliseconds(start);
		restoreGeneratedModule(context, previous, entry);
	}
	consumeDebugIRProducts(*context.llvmModule);
	return buildLLVMArtifact(
		context, directory, compilerIdentity, options, previous.at("generated_ir").get_ref<const std::string &>(), statistics
	);
}
