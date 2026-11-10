std::set<std::string> liveOptimizedUnits(const IncrementalOptimization &optimization) {
	std::unordered_map<std::string_view, std::vector<const std::string *>> owners;
	std::set<std::string> live;
	std::vector<const std::string *> pending;
	auto visit = [&](const std::string &unit) {
		if (live.insert(unit).second)
			pending.push_back(&unit);
	};
	for (const auto &[unit, summary] : optimization.outputs) {
		bool root = summary.at("assembly").get<bool>();
		for (const Json &definition : summary.at("definitions")) {
			const std::string &name = definition.at(0).get_ref<const std::string &>();
			owners[definition.at(1).get_ref<const std::string &>()].push_back(&unit);
			// Pre-link protection can turn a source link-once definition into a
			// weak definition. Its original visibility still decides if it is a root.
			root |= optimization.preserved.contains(name) ||
					(!optimization.definitions.contains(name) && definition.at(2).get<bool>());
		}
		if (root)
			visit(unit);
	}
	while (!pending.empty()) {
		const std::string &unit = *pending.back();
		pending.pop_back();
		for (const Json &reference : optimization.outputs.at(unit).at("references")) {
			auto found = owners.find(reference.get_ref<const std::string &>());
			if (found != owners.end())
				for (const std::string *owner : found->second)
					visit(*owner);
		}
	}
	return live;
}

struct NativeUnitWork {
	std::string name;
	Json record;
	std::optional<std::string> object, bitcode;
	const std::string *generatedBitcode = nullptr;
};

std::vector<NativeUnitWork>
planOptimizedNativeUnits(const IncrementalOptimization &optimization, const std::set<std::string> &live, const Json *previous) {
	std::vector<NativeUnitWork> plan;
	plan.reserve(live.size());
	for (const std::string &name : live) {
		const Json &output = optimization.outputs.at(name);
		const std::string &bitcodeKey = output.at("bitcode_key").get_ref<const std::string &>();
		std::string input = digest(optimization.cacheIdentity + name + bitcodeKey);
		Json objectKey = nullptr;
		if (previous && previous->contains(name) && previous->at(name).at("input_key") == input)
			objectKey = previous->at(name).at("object_key");
		else if (auto cached = readEntry(optimization.directory / ("native-unit-" + input + ".entry")))
			objectKey = std::move(*cached);
		plan.push_back(
			{name,
			 Json{
				 {"input_key", std::move(input)},
				 {"bitcode_key", bitcodeKey},
				 {"object_key", std::move(objectKey)},
				 {"functions", output.at("functions")}
			 },
			 std::nullopt, std::nullopt, nullptr}
		);
	}
	return plan;
}

bool nativeLinkUnchanged(
	const ParseContext &context, const Json *previous, const std::vector<std::string> &libraries,
	const std::vector<std::string> &keys
) {
	return previous && previous->at("object_keys") == keys && previous->at("libraries") == libraries &&
		   linkedOutputUnchanged(context, *previous);
}

void recordOptimizedNativeUnits(std::vector<NativeUnitWork> &plan, BuildStatistics &statistics) {
	statistics.nativeUnits = Json{{"units", Json::object()}};
	statistics.objectKeys.clear();
	statistics.objectKeys.reserve(plan.size());
	for (NativeUnitWork &unit : plan) {
		statistics.nativeFunctions += unit.record.at("functions").get<size_t>();
		statistics.objectKeys.push_back(unit.record.at("object_key").get<std::string>());
		statistics.nativeUnits["units"][unit.name] = std::move(unit.record);
	}
}

std::set<std::string> loadNativeUnitInputs(
	const fs::path &directory, std::vector<NativeUnitWork> &plan,
	const std::map<std::string, std::string> *materialized = nullptr
) {
	std::set<std::string> missing;
	for (NativeUnitWork &unit : plan) {
		if (unit.record.at("object_key").is_string())
			unit.object = readEntry(directory / ("object-" + unit.record.at("object_key").get<std::string>() + ".entry"));
		if (unit.object)
			continue;
		if (materialized) {
			auto found = materialized->find(unit.name);
			if (found != materialized->end()) {
				unit.generatedBitcode = &found->second;
				continue;
			}
		}
		const std::string &key = unit.record.at("bitcode_key").get_ref<const std::string &>();
		unit.bitcode = readEntry(programPayloadPath(directory, key));
		if (!unit.bitcode)
			missing.insert(unit.name);
		else if (digest(*unit.bitcode) != key)
			throw std::runtime_error("corrupt optimized bitcode: " + key);
	}
	return missing;
}

bool emitOptimizedNativeUnits(
	ParseContext &context, const fs::path &directory, std::vector<NativeUnitWork> &plan,
	const std::vector<std::string> &libraries, BuildStatistics &statistics, const Json *previous
) {
	requireCompilerInvariant(!plan.empty(), "native program has no live optimized units");
	auto start = Clock::now();
	TemporaryObjects temporary;
	std::vector<std::string> objects;
	objects.reserve(plan.size());
	for (NativeUnitWork &unit : plan) {
		fs::path path = temporary.directory / (std::to_string(objects.size()) + ".o");
		objects.push_back(path.string());
		if (unit.object) {
			statistics.reusedObjects++;
			continue;
		}
		const std::string &bitcode = unit.generatedBitcode ? *unit.generatedBitcode : unit.bitcode.value();
		llvm::LLVMContext unitContext;
		auto module = parseCachedModule(bitcode, unitContext);
		module->setModuleIdentifier(unit.name);
		requireCompilerInvariant(!llvm::verifyModule(*module), "cached optimized unit is invalid");
		if (!emitNativeObjectModule(context, *module, path.string()))
			return false;
		std::string bytes = readFile(path);
		std::string key = digest(bytes);
		writeEntry(directory / ("object-" + key + ".entry"), bytes);
		writeEntry(directory / ("native-unit-" + unit.record.at("input_key").get<std::string>() + ".entry"), key);
		unit.record["object_key"] = std::move(key);
		statistics.builtObjects++;
		statistics.nativeFunctionsLowered += unit.record.at("functions").get<size_t>();
	}
	recordOptimizedNativeUnits(plan, statistics);
	if (nativeLinkUnchanged(context, previous, libraries, statistics.objectKeys)) {
		statistics.nativeUnitMilliseconds += elapsedMilliseconds(start);
		return true;
	}
	for (size_t index = 0; index < plan.size(); index++) {
		if (!plan[index].object)
			continue;
		std::ofstream output(objects[index], std::ios::binary);
		output.write(plan[index].object->data(), plan[index].object->size());
		output.close();
		if (!output)
			throw std::runtime_error("cannot materialize native object: " + objects[index]);
	}
	statistics.nativeUnitMilliseconds += elapsedMilliseconds(start);
	auto linkStart = Clock::now();
	bool linked = emitNativeExecutableWithObjects(context, objects, libraries);
	statistics.linkMilliseconds += elapsedMilliseconds(linkStart);
	statistics.linkedCachedObjects = statistics.builtObjects == 0;
	return linked;
}

void loadNativeUnitSnapshot(Json &previous, const fs::path &directory, BuildStatistics &statistics) {
	auto start = Clock::now();
	statistics.cachePayloadsLoaded += hydrateProgramField(previous, directory, "native_units");
	statistics.cachePayloadLoadMilliseconds += elapsedMilliseconds(start);
}

bool buildNativeOutput(
	ParseContext &context, const fs::path &directory, const std::string &compilerIdentity, const Json &options,
	const std::vector<std::string> &libraries, BuildStatistics &statistics, Json *previous,
	const IncrementalIRReuse *functionReuse, bool restoredModule = false
) {
	if (!context.options.optimizationLevel)
		return emitPartitions(
			context, directory, compilerIdentity, options, libraries, statistics, previous, functionReuse, restoredModule
		);
	const Json *priorUnits = nullptr;
	if (previous) {
		loadNativeUnitSnapshot(*previous, directory, statistics);
		priorUnits = &previous->at("native_units").at("units");
	}
	IncrementalOptimization optimization(context, directory, compilerIdentity, options, previous, statistics, functionReuse);
	optimization.run();
	for (;;) {
		auto start = Clock::now();
		std::set<std::string> live = liveOptimizedUnits(optimization);
		statistics.nativeReachabilityMilliseconds += elapsedMilliseconds(start);
		statistics.nativeUnreachableUnits = optimization.outputs.size() - live.size();
		auto plan = planOptimizedNativeUnits(optimization, live, priorUnits);
		std::vector<std::string> keys;
		for (const NativeUnitWork &unit : plan)
			if (unit.record.at("object_key").is_string())
				keys.push_back(unit.record.at("object_key").get<std::string>());
		if (keys.size() == plan.size() && nativeLinkUnchanged(context, previous, libraries, keys)) {
			recordOptimizedNativeUnits(plan, statistics);
			statistics.nativeUnitMilliseconds += elapsedMilliseconds(start);
			return true;
		}
		auto missing = loadNativeUnitInputs(directory, plan, &optimization.materializedBitcode);
		statistics.nativeUnitMilliseconds += elapsedMilliseconds(start);
		if (missing.empty())
			return emitOptimizedNativeUnits(context, directory, plan, libraries, statistics, previous);
		optimization.run(missing);
		for (const std::string &unit : missing)
			requireCompilerInvariant(
				optimization.materializedBitcode.contains(unit), "requested optimized unit was not materialized"
			);
	}
}

bool linkOrRestoreNativeOutput(
	ParseContext &context, const fs::path &directory, const fs::path &entry, const std::string &compilerIdentity,
	const Json &options, const std::vector<std::string> &libraries, BuildStatistics &statistics, Json &previous,
	bool generatedModuleAvailable = false
) {
	statistics.objectKeys = previous.at("object_keys").get<std::vector<std::string>>();
	if (auto linked = linkCachedObjects(context, directory, statistics.objectKeys, libraries, statistics))
		return *linked;
	if (context.options.optimizationLevel) {
		auto start = Clock::now();
		loadNativeUnitSnapshot(previous, directory, statistics);
		std::vector<NativeUnitWork> plan;
		for (const auto &[name, record] : previous.at("native_units").at("units").items())
			plan.push_back({name, record, std::nullopt, std::nullopt, nullptr});
		auto missing = loadNativeUnitInputs(directory, plan);
		statistics.nativeUnitMilliseconds += elapsedMilliseconds(start);
		if (missing.empty())
			return emitOptimizedNativeUnits(context, directory, plan, libraries, statistics, &previous);
	}
	if (!generatedModuleAvailable) {
		auto start = Clock::now();
		statistics.cachePayloadsLoaded += hydrateProgramField(previous, directory, "generated_ir");
		statistics.cachePayloadLoadMilliseconds += elapsedMilliseconds(start);
		restoreGeneratedModule(context, previous, entry);
	}
	statistics.objectKeys.clear();
	return buildNativeOutput(context, directory, compilerIdentity, options, libraries, statistics, &previous, nullptr, true);
}

void recordNativeOutput(Json &program, BuildStatistics &statistics, const Json *previous) {
	program["object_keys"] = statistics.objectKeys;
	auto save = [&](const char *field, Json &value) {
		if (!value.is_null())
			program[field] = std::move(value);
		else if (previous)
			program["payload_keys"][field] = previous->at("payload_keys").at(field);
		else
			program[field] = Json::object();
	};
	save("native_units", statistics.nativeUnits);
	save("optimization_units", statistics.optimizationUnits);
}
