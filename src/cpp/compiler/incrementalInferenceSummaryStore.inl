// The manifest contains small references; type tables and observed values are
// decoded only for the concrete variants consumed by this build.
class InferenceSummaryStore {
  public:
	InferenceSummaryStore(const fs::path &directory, BuildStatistics &statistics)
		: directory(directory), statistics(statistics) {}
	struct BodyTransferWindow {
		InferenceSummaryStore &store;
		BodyTransferWindow *previous;
		std::map<std::string, Json> bodies;
		explicit BodyTransferWindow(InferenceSummaryStore &store) : store(store), previous(store.bodyTransfers) {
			store.bodyTransfers = this;
		}
		BodyTransferWindow(const BodyTransferWindow &) = delete;
		BodyTransferWindow &operator=(const BodyTransferWindow &) = delete;
		~BodyTransferWindow() {
			requireCompilerInvariant(store.bodyTransfers == this, "inferred body transfer scopes closed out of order");
			store.bodyTransfers = previous;
		}
	};

	void retainReplacementBody(const Json &reference) {
		if (reference.is_null())
			return;
		const auto &key = reference.at("payload").get_ref<const std::string &>();
		if (replacementBodies.contains(key))
			return;
		for (auto *window = bodyTransfers; window; window = window->previous) {
			auto product = window->bodies.extract(key);
			if (!product.empty()) {
				replacementBodies.insert(std::move(product));
				return;
			}
		}
	}
	void releaseReplacementBodies() { replacementBodies.clear(); }
	void observe(const Json &keys, const Json &bodies, const Json &classes) {
		for (const Json &key : keys) {
			const std::string &identity = key.get_ref<const std::string &>();
			available[identity] = recordCacheAccess(inferenceSummaryPath(directory, identity));
		}
		for (const Json &key : bodies) {
			const std::string &identity = key.get_ref<const std::string &>();
			availableBodies[identity] = recordCacheAccess(bodyPath(identity));
		}
		for (const Json &key : classes) {
			const auto &identity = key.get_ref<const std::string &>();
			availableClasses[identity] = recordCacheAccess(classPath(identity));
		}
	}
	Json saveBody(Json body, size_t valueReferences, size_t instantiationReferences, double captureMilliseconds) {
		auto start = Clock::now();
		Json classes = body.at("classes");
		std::string payload = body.dump(), key = digest(payload);
		statistics.inferenceBodiesEncoded++;
		statistics.inferenceBodyValuesEncoded += body.at("values").size();
		statistics.inferenceBodyValueReferences += valueReferences;
		statistics.inferenceBodyInstantiationsEncoded += body.at("instances").size();
		statistics.inferenceBodyInstantiationReferences += instantiationReferences;
		if (!recordCacheAccess(bodyPath(key))) {
			writeEntry(bodyPath(key), payload);
			statistics.inferenceBodiesWritten++;
		}
		availableBodies[key] = true;
		statistics.inferenceBodyCaptureMilliseconds += captureMilliseconds;
		statistics.inferenceBodyEncodeMilliseconds += captureMilliseconds + elapsedMilliseconds(start);
		Json reference = Json::object();
		reference["payload"] = key;
		reference["dependencies"] = body.at("dependencies");
		reference["classes"] = std::move(classes);
		reference["for_values"] = body.at("for_values");
		if (bodyTransfers)
			bodyTransfers->bodies.try_emplace(key, std::move(body));
		return reference;
	}
	std::optional<Json> loadClasses(const Json &references, const std::set<std::string> &required) {
		Json result = Json::object();
		std::stack<std::string> pending;
		for (const auto &path : required)
			pending.push(path);
		while (!pending.empty()) {
			std::string path = std::move(pending.top());
			pending.pop();
			if (result.contains(path))
				continue;
			if (!references.contains(path))
				return std::nullopt;
			const auto &reference = references.at(path);
			const auto &key = reference.at("payload").get_ref<const std::string &>();
			if (!availableClasses.at(key))
				return std::nullopt;
			if (!loadedClasses.contains(key)) {
				auto payload = readEntry(classPath(key));
				if (!payload)
					throw std::runtime_error("inferred class disappeared: " + key);
				if (digest(*payload) != key)
					throw std::runtime_error("corrupt inferred class: " + key);
				loadedClasses[key] = Json::parse(*payload);
				statistics.inferenceClassesLoaded++;
			}
			result[path] = loadedClasses.at(key);
			for (const Json &dependency : reference.at("dependencies"))
				pending.push(dependency.get<std::string>());
		}
		return result;
	}
	void recordClassRestored() { statistics.inferenceClassesRestored++; }
	void recordClassSnapshotCapture() { statistics.inferenceClassStatesCaptured++; }
	std::optional<Json> loadBody(const Json &reference) {
		if (reference.is_null())
			return std::nullopt;
		const auto &key = reference.at("payload").get_ref<const std::string &>();
		if (!availableBodies.at(key))
			return std::nullopt;
		auto replacement = replacementBodies.extract(key);
		if (!replacement.empty()) {
			statistics.inferenceBodyTransfers++;
			return std::move(replacement.mapped());
		}
		for (auto *window = bodyTransfers; window; window = window->previous) {
			auto product = window->bodies.extract(key);
			if (!product.empty()) {
				statistics.inferenceBodyTransfers++;
				return std::move(product.mapped());
			}
		}
		auto payload = readEntry(bodyPath(key));
		if (!payload)
			throw std::runtime_error("inferred body disappeared: " + key);
		if (digest(*payload) != key)
			throw std::runtime_error("corrupt inferred body: " + key);
		return Json::parse(*payload);
	}

	const Json *load(const Json &reference) {
		const std::string &key = reference.at("payload").get_ref<const std::string &>();
		if (auto found = loaded.find(key); found != loaded.end())
			return &found->second;
		if (!available.at(key))
			return nullptr;
		auto start = Clock::now();
		auto payload = readEntry(inferenceSummaryPath(directory, key));
		if (!payload)
			throw std::runtime_error("inference summary disappeared: " + key);
		if (digest(*payload) != key)
			throw std::runtime_error("corrupt inference summary: " + key);
		Json summary = decodeInferenceSummary(Json::parse(*payload));
		statistics.inferenceSummaryUnitsLoaded++;
		statistics.inferenceSummaryLoadMilliseconds += elapsedMilliseconds(start);
		return &loaded.emplace(key, std::move(summary)).first->second;
	}

	bool retain(const Json &reference) const { return available.at(reference.at("payload").get_ref<const std::string &>()); }

	Json save(const Json &summary, Json body, Json classes, Json diagnostics) {
		auto start = Clock::now();
		std::string payload = encodeInferenceSummary(summary).dump();
		std::string key = digest(payload);
		statistics.inferenceSummaryUnitsEncoded++;
		fs::path path = inferenceSummaryPath(directory, key);
		if (!recordCacheAccess(path)) {
			writeEntry(path, payload);
			statistics.inferenceSummaryUnitsWritten++;
		}
		available[key] = true;
		statistics.inferenceSummaryEncodeMilliseconds += elapsedMilliseconds(start);
		SymbolicInferenceUnits dependencies;
		for (const Json &target : captureSymbolicInferenceDependencies(summary))
			dependencies.emplace(target.at(0).get<std::string>(), target.at(1).get<std::string>());
		for (const Json &reference : classes)
			for (const Json &target : reference.at("symbolic_dependencies"))
				dependencies.emplace(target.at(0).get<std::string>(), target.at(1).get<std::string>());
		Json result{
			{"diagnostics", std::move(diagnostics)},
			{"body", std::move(body)},
			{"classes", std::move(classes)},
			{"payload", std::move(key)},
			{"global_inputs", summary.at("global_inputs")},
			{"symbolic_dependencies", dependencies},
			{"failed", !summary.at("failure").is_null()}
		};
		if (summary.at("failure").is_null())
			result.update(
				Json{
					{"purity", summary.at("purity")},
					{"observed_values", !summary.at("pure_values").empty()},
					{"pure_values_complete", summary.at("pure_values_complete")}
				}
			);
		return result;
	}

	Json saveClass(const Json &state) {
		auto started = Clock::now();
		statistics.inferenceClassRecordRequests++;
		if (auto found = classReferences.find(state); found != classReferences.end()) {
			statistics.inferenceClassRecordMilliseconds += elapsedMilliseconds(started);
			return found->second;
		}
		std::string payload = state.dump(), key = digest(payload);
		auto dependencies = classInstantiationDependencies(state);
		SymbolicInferenceUnits symbolic;
		if (!state.at("requests").is_null())
			for (const Json &request : state.at("requests"))
				for (const Json &parameter : request.at(0))
					collectSymbolicInferenceValue(parameter.at(2), symbolic);
		if (!recordCacheAccess(classPath(key))) {
			writeEntry(classPath(key), payload);
			statistics.inferenceClassesWritten++;
		}
		availableClasses[key] = true;
		Json reference{{"payload", key}, {"dependencies", dependencies}, {"symbolic_dependencies", symbolic}};
		classReferences.emplace(state, reference);
		statistics.inferenceClassRecordMilliseconds += elapsedMilliseconds(started);
		return reference;
	}

  private:
	fs::path classPath(const std::string &key) const { return directory / ("inference-class-" + key + ".entry"); }
	fs::path bodyPath(const std::string &key) const { return directory / ("inference-body-" + key + ".entry"); }
	const fs::path &directory;
	BuildStatistics &statistics;
	BodyTransferWindow *bodyTransfers = nullptr;
	std::map<std::string, Json> replacementBodies;
	std::map<std::string, Json> loaded;
	std::map<std::string, bool> available;
	std::map<std::string, bool> availableBodies;
	std::map<std::string, bool> availableClasses;
	std::map<std::string, Json> loadedClasses;
	std::map<Json, Json> classReferences;
};
