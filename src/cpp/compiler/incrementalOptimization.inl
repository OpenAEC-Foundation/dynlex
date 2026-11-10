Json summarizeOptimizedUnit(const llvm::Module &module) {
	llvm::Mangler mangler;
	auto objectName = [&](const llvm::GlobalValue &value) {
		llvm::SmallString<128> name;
		mangler.getNameWithPrefix(name, &value, false);
		return name.str().str();
	};
	Json definitions = Json::array();
	std::set<std::string> references;
	std::vector<const llvm::GlobalValue *> pending;
	size_t functions = 0;
	for (const llvm::GlobalValue &value : module.global_values()) {
		if (value.isDeclarationForLinker())
			continue;
		pending.push_back(&value);
		functions += llvm::isa<llvm::Function>(value);
		if (!value.hasLocalLinkage())
			definitions.push_back(Json::array({value.getName().str(), objectName(value), !value.isDiscardableIfUnused()}));
	}
	std::unordered_set<const llvm::GlobalValue *> visited;
	while (!pending.empty()) {
		const llvm::GlobalValue *value = pending.back();
		pending.pop_back();
		if (!visited.insert(value).second)
			continue;
		for (const llvm::GlobalValue *reference : NativeSymbolReferences{}.collectGlobals(*value)) {
			if (!reference->hasLocalLinkage())
				references.insert(objectName(*reference));
			// The native backend can still use a referenced imported definition
			// (for example, a constant initializer). Unused imports have no edge.
			if (!reference->isDeclaration() && (reference->hasLocalLinkage() || reference->isDeclarationForLinker()))
				pending.push_back(reference);
		}
	}
	bool assembly = !module.getModuleInlineAsm().empty();
	if (assembly)
		llvm::ModuleSymbolTable::CollectAsmSymbols(module, [&](llvm::StringRef name, llvm::object::BasicSymbolRef::Flags flags) {
			if (flags & llvm::object::BasicSymbolRef::SF_Undefined)
				references.insert(name.str());
		});
	return Json{
		{"definitions", std::move(definitions)}, {"references", std::move(references)}, {"functions", functions},
		{"assembly", assembly}
	};
}

struct IncrementalOptimization {
	ParseContext &context;
	const fs::path &directory;
	BuildStatistics &statistics;
	std::string cacheIdentity;
	std::set<std::string> definitions, preserved;
	std::vector<OptimizationInput> inputs;
	std::map<std::string, Json> outputs;
	std::map<std::string, std::string> materializedBitcode;

	IncrementalOptimization(
		ParseContext &context, const fs::path &directory, const std::string &compilerIdentity, const Json &options,
		Json *previous, BuildStatistics &statistics, const IncrementalIRReuse *functionReuse
	)
		: context(context), directory(directory), statistics(statistics), cacheIdentity(compilerIdentity + options.dump()) {
		auto start = Clock::now();
		if (previous) {
			auto payloadStart = Clock::now();
			statistics.cachePayloadsLoaded += hydrateProgramField(*previous, directory, "optimization_units");
			statistics.cachePayloadLoadMilliseconds += elapsedMilliseconds(payloadStart);
		}
		inputs = captureOptimizationInputs(
			context, directory, previous ? &previous->at("optimization_units") : nullptr, statistics, functionReuse, preserved
		);
		for (const llvm::GlobalValue &value : context.llvmModule->global_values())
			if (!value.isDeclaration())
				definitions.insert(value.getName().str());
		statistics.optimizationInputMilliseconds += elapsedMilliseconds(start);
	}

	void run(const std::set<std::string> &materialize = {}) {
		auto indexStart = Clock::now();
		std::mutex mutex;
		std::exception_ptr failure;
		std::map<unsigned, std::pair<std::string, std::string>> tasks;
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
					identity = tasks.at(task);
				}
				std::string bitcode = serializeCachedModule(module);
				std::string key = digest(bitcode);
				fs::path path = programPayloadPath(directory, key);
				if (!recordCacheAccess(path))
					writeEntry(path, bitcode);
				Json result = summarizeOptimizedUnit(module);
				result["bitcode_key"] = std::move(key);
				writeEntry(directory / ("optimization-" + identity.first + ".entry"), result.dump());
				std::lock_guard lock(mutex);
				outputs.insert_or_assign(identity.second, std::move(result));
				materializedBitcode.insert_or_assign(identity.second, std::move(bitcode));
				statistics.optimizationUnitsBuilt++;
			} catch (...) {
				std::lock_guard lock(mutex);
				failure = std::current_exception();
			}
			// Native lowering consumes only units reached from the optimized graph.
			return false;
		};
		std::vector<std::unique_ptr<llvm::MemoryBuffer>> buffers;
		llvm::lto::LTO optimizer(
			std::move(configuration), llvm::lto::createInProcessThinBackend(llvm::heavyweight_hardware_concurrency(1))
		);
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
		statistics.optimizationIndexMilliseconds += elapsedMilliseconds(indexStart);
		llvm::FileCache cache(
			[&](unsigned task, llvm::StringRef key, const llvm::Twine &module) -> llvm::Expected<llvm::AddStreamFn> {
				try {
					std::string name = module.str();
					if (!materialize.empty() && !materialize.contains(name))
						return llvm::AddStreamFn{};
					std::string identity = digest(cacheIdentity + key.str());
					if (materialize.empty())
						if (auto payload = readEntry(directory / ("optimization-" + identity + ".entry"))) {
							Json result = Json::parse(*payload);
							std::lock_guard lock(mutex);
							outputs.emplace(std::move(name), std::move(result));
							statistics.optimizationUnitsReused++;
							return llvm::AddStreamFn{};
						}
					std::lock_guard lock(mutex);
					tasks.emplace(task, std::pair{std::move(identity), std::move(name)});
					return llvm::AddStreamFn(
						[](unsigned, const llvm::Twine &) -> llvm::Expected<std::unique_ptr<llvm::CachedFileStream>> {
							crashCompilerBug("optimization query reached native lowering");
						}
					);
				} catch (const std::exception &error) {
					return llvm::createStringError(std::make_error_code(std::errc::io_error), llvm::Twine(error.what()));
				}
			}, directory.string()
		);
		auto runStart = Clock::now();
		llvm::Error error =
			optimizer.run([](unsigned, const llvm::Twine &) -> llvm::Expected<std::unique_ptr<llvm::CachedFileStream>> {
				crashCompilerBug("optimization query emitted an uncached native object");
			}, std::move(cache));
		statistics.optimizationBackendMilliseconds += elapsedMilliseconds(runStart);
		statistics.optimizationMilliseconds =
			statistics.optimizationInputMilliseconds + statistics.optimizationIndexMilliseconds + statistics.optimizationBackendMilliseconds;
		if (failure) {
			llvm::consumeError(std::move(error));
			std::rethrow_exception(failure);
		}
		if (error)
			throw std::runtime_error("LLVM optimization failed: " + llvm::toString(std::move(error)));
	}
};
