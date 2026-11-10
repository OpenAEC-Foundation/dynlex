bool optimizeIncrementalModule(
	ParseContext &context, const fs::path &directory, const std::string &compilerIdentity, const Json &options, Json *previous,
	BuildStatistics &statistics, const IncrementalIRReuse *functionReuse
) {
	if (!context.options.optimizationLevel)
		return true;
	std::map<std::string, std::pair<llvm::GlobalValue::LinkageTypes, llvm::GlobalValue::VisibilityTypes>> localSymbols;
	for (const llvm::GlobalValue &value : context.llvmModule->global_values())
		if (value.hasLocalLinkage())
			localSymbols.emplace(value.getName().str(), std::pair{value.getLinkage(), value.getVisibility()});
	auto inputStart = Clock::now();
	if (previous) {
		auto payloadStart = Clock::now();
		statistics.cachePayloadsLoaded += hydrateProgramField(*previous, directory, "optimization_units");
		statistics.cachePayloadLoadMilliseconds += elapsedMilliseconds(payloadStart);
	}
	std::set<std::string> preserved;
	std::vector<OptimizationInput> inputs = captureOptimizationInputs(
		context, directory, previous ? &previous->at("optimization_units") : nullptr, statistics, functionReuse, preserved
	);
	statistics.optimizationInputMilliseconds = elapsedMilliseconds(inputStart);
	std::mutex mutex;
	std::exception_ptr failure;
	std::map<unsigned, std::pair<std::string, std::string>> tasks;
	std::map<std::string, std::string> optimizedModules;
	std::string cacheIdentity = compilerIdentity + options.dump();
	llvm::lto::Config configuration;
	configuration.CPU = context.targetMachine->getTargetCPU().str();
	configuration.Options = llvmTargetOptions(context.options);
	configuration.OptLevel = context.options.optimizationLevel;
	configuration.CGOptLevel = codeGenerationOptimizationLevel(context.options);
	configuration.RelocModel = context.targetMachine->getRelocationModel();
	llvm::SmallVector<llvm::StringRef> features;
	context.targetMachine->getTargetFeatureString().split(features, ',', -1, false);
	for (llvm::StringRef feature : features)
		configuration.MAttrs.push_back(feature.str());
	if (context.options.loopVectorization)
		configuration.PTO.LoopVectorization = *context.options.loopVectorization;
	if (context.options.slpVectorization)
		configuration.PTO.SLPVectorization = *context.options.slpVectorization;
	if (context.options.loopUnrolling)
		configuration.PTO.LoopUnrolling = *context.options.loopUnrolling;
	configuration.DiagHandler = [&](const llvm::DiagnosticInfo &diagnostic) {
		std::string message;
		llvm::raw_string_ostream stream(message);
		llvm::DiagnosticPrinterRawOStream printer(stream);
		diagnostic.print(printer);
		stream.flush();
		std::lock_guard lock(mutex);
		if (diagnostic.getSeverity() == llvm::DS_Error)
			failure = std::make_exception_ptr(std::runtime_error("LLVM optimization failed: " + message));
		else
			llvm::errs() << message << '\n';
	};
	configuration.PostOptModuleHook = [&](unsigned task, const llvm::Module &module) {
		try {
			std::pair<std::string, std::string> identity;
			{
				std::lock_guard lock(mutex);
				if (failure)
					return false;
				requireCompilerInvariant(tasks.contains(task), "optimized unit has no ThinLTO cache identity");
				identity = tasks.at(task);
			}
			// Internal helpers introduced independently in two backends must keep
			// independent names when their optimized modules are combined.
			auto result = llvm::CloneModule(module);
			std::string prefix = "__dynlex_unit_" + digest(identity.second) + "_";
			for (llvm::GlobalValue &value : result->global_values())
				if (value.hasLocalLinkage())
					value.setName(prefix + value.getName().str());
			sortModuleSymbols(*result);
			std::string bitcode = serializeCachedModule(*result);
			writeEntry(directory / ("optimization-" + identity.first + ".entry"), bitcode);
			std::lock_guard lock(mutex);
			optimizedModules.emplace(identity.second, std::move(bitcode));
			statistics.optimizationUnitsBuilt++;
		} catch (...) {
			std::lock_guard lock(mutex);
			failure = std::current_exception();
		}
		// The caller consumes optimized IR. Native emission is a separate query.
		return false;
	};
	std::vector<std::unique_ptr<llvm::MemoryBuffer>> buffers;
	llvm::lto::LTO optimizer(
		std::move(configuration), llvm::lto::createInProcessThinBackend(llvm::heavyweight_hardware_concurrency(1))
	);
	std::set<std::string> definitions;
	for (const llvm::GlobalValue &value : context.llvmModule->global_values())
		if (!value.isDeclaration())
			definitions.insert(value.getName().str());
	for (const OptimizationInput &unit : inputs) {
		buffers.push_back(llvm::MemoryBuffer::getMemBufferCopy(unit.bitcode, unit.name));
		auto input = llvm::lto::InputFile::create(buffers.back()->getMemBufferRef());
		if (!input)
			throw std::runtime_error("cannot read optimization input: " + llvm::toString(input.takeError()));
		std::vector<llvm::lto::SymbolResolution> resolutions;
		for (const auto &symbol : (*input)->symbols()) {
			llvm::lto::SymbolResolution resolution;
			resolution.Prevailing = !symbol.isUndefined();
			resolution.FinalDefinitionInLinkageUnit = definitions.contains(symbol.getIRName().str());
			resolution.VisibleToRegularObj = preserved.contains(symbol.getIRName().str()) || symbol.isUsed();
			resolutions.push_back(resolution);
		}
		if (llvm::Error error = optimizer.add(std::move(*input), resolutions))
			throw std::runtime_error("cannot index optimization input: " + llvm::toString(std::move(error)));
	}
	llvm::FileCache cache(
		[&](unsigned task, llvm::StringRef key, const llvm::Twine &module) -> llvm::Expected<llvm::AddStreamFn> {
		try {
			std::string identity = digest(cacheIdentity + key.str());
			std::string name = module.str();
			if (auto bitcode = readEntry(directory / ("optimization-" + identity + ".entry"))) {
				std::lock_guard lock(mutex);
				statistics.optimizationUnits["outputs"][name] = identity;
				optimizedModules.emplace(std::move(name), std::move(*bitcode));
				statistics.optimizationUnitsReused++;
				return llvm::AddStreamFn{};
			}
			std::lock_guard lock(mutex);
			statistics.optimizationUnits["outputs"][name] = identity;
			tasks.emplace(task, std::pair{std::move(identity), std::move(name)});
			return llvm::AddStreamFn(
				[](unsigned, const llvm::Twine &) -> llvm::Expected<std::unique_ptr<llvm::CachedFileStream>> {
				crashCompilerBug("optimization query reached native emission");
			}
			);
		} catch (const std::exception &error) {
			return llvm::createStringError(std::make_error_code(std::errc::io_error), llvm::Twine(error.what()));
		}
	}, directory.string()
	);
	llvm::Error error =
		optimizer.run([](unsigned, const llvm::Twine &) -> llvm::Expected<std::unique_ptr<llvm::CachedFileStream>> {
		crashCompilerBug("optimization query emitted an uncached native object");
	}, std::move(cache));
	if (failure) {
		llvm::consumeError(std::move(error));
		std::rethrow_exception(failure);
	}
	if (error)
		throw std::runtime_error("LLVM optimization failed: " + llvm::toString(std::move(error)));
	auto combineStart = Clock::now();
	auto combined = std::make_unique<llvm::Module>(context.llvmModule->getModuleIdentifier(), *context.llvmContext);
	combined->setSourceFileName(context.llvmModule->getSourceFileName());
	combined->setDataLayout(context.llvmModule->getDataLayout());
	combined->setTargetTriple(context.llvmModule->getTargetTriple());
	llvm::Linker linker(*combined);
	for (const auto &[_, bitcode] : optimizedModules) {
		auto part = parseCachedModule(bitcode, *context.llvmContext);
		requireCompilerInvariant(!linker.linkInModule(std::move(part)), "optimized units disagree");
	}
	std::map<std::string, std::string> originalNames;
	for (const auto &[original, normalized] : statistics.optimizationUnits.at("names").items())
		originalNames.emplace(normalized.get<std::string>(), original);
	for (llvm::GlobalValue &value : combined->global_values()) {
		if (value.isDeclaration())
			continue;
		auto original = originalNames.find(value.getName().str());
		if (original == originalNames.end())
			continue;
		const auto &[linkage, visibility] = localSymbols.at(original->second);
		value.setLinkage(linkage);
		value.setVisibility(visibility);
		value.setName(original->second);
	}
	// Cross-unit imports can inline every call while retaining the exporting
	// definition. Its original local linkage makes that definition removable.
	llvm::ModuleAnalysisManager analyses;
	llvm::GlobalDCEPass{}.run(*combined, analyses);
	sortModuleSymbols(*combined);
	requireCompilerInvariant(!llvm::verifyModule(*combined), "combined optimized module is invalid");
	delete context.llvmModule;
	context.llvmModule = combined.release();
	statistics.optimizationCombineMilliseconds = elapsedMilliseconds(combineStart);
	return true;
}
