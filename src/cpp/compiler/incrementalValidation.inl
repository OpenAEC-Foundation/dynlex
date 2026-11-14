// Capture checks observe individual scope queries. Literal values, unrelated
// bindings and unexecuted definitions cannot change their lexical result.
class IncrementalValidation {
	using Read = std::tuple<Section *, ValidationRead, std::string>;
	ParseContext &context;
	BuildStatistics &statistics;
	IncrementalSourceScopes &sourceScopes;
	std::function<Json()> load;
	Json records = Json::object();
	bool loaded = false, prepared = false;
	std::map<const Section *, std::string> paths;
	std::map<std::string, Section *> sections;
	std::map<const VariableReference *, Json> declarations;
	std::map<Read, std::string> fingerprints;
	std::unique_ptr<InferenceValueIdentities> identities;
	std::unique_ptr<InferenceBodySources> sourceAnchors;
	std::map<Read, std::string> *reads{};

	InferenceBodySources &sources() {
		if (!sourceAnchors) {
			identities = std::make_unique<InferenceValueIdentities>(paths);
			sourceAnchors = std::make_unique<InferenceBodySources>(context, *identities);
		}
		return *sourceAnchors;
	}
	void prepare() {
		if (prepared)
			return;
		loadRecords();
		paths = sourceScopes.identities();
		sections = sourceScopes.compact();
		for (const auto &[section, path] : paths)
			for (const auto &[name, reference] : section->variableDefinitions)
				declarations.emplace(reference, Json::array({path, name}));
		for (auto record = records.begin(); record != records.end();)
			if (!sections.contains(record.key()))
				record = records.erase(record);
			else
				++record;
		prepared = true;
	}
	void loadRecords() {
		if (!loaded) {
			records = load();
			loaded = true;
		}
	}
	const std::string &fingerprint(const Read &read) {
		auto [found, inserted] = fingerprints.try_emplace(read);
		if (!inserted)
			return found->second;
		const auto &[section, kind, name] = read;
		Json inputs = Json::array();
		switch (kind) {
		case ValidationRead::Definition:
			inputs = Json::array({static_cast<int>(section->type), section->isFlex, section->receiverParameter != nullptr});
			break;
		case ValidationRead::Children:
			for (Section *child : section->children)
				if (child->runsInEnclosingBody())
					inputs.push_back(paths.at(child));
			break;
		case ValidationRead::References: {
			std::vector<Json> canonical;
			for (const auto &[name, group] : section->variableReferences)
				for (VariableReference *reference : group) {
					statistics.validation.referencesFingerprinted++;
					canonical.push_back(Json::array({name, declarations.at(normalizeBindingReference(reference))}));
				}
			std::sort(canonical.begin(), canonical.end());
			inputs = std::move(canonical);
			break;
		}
		case ValidationRead::DiagnosticOrder:
			for (VariableReference *reference : captureReferences(section))
				inputs.push_back(sources().range(reference->range));
			break;
		case ValidationRead::Binding:
			if (auto variable = section->variables.find(name); variable != section->variables.end())
				inputs = Json::array({declarations.at(variable->second->definition), variable->second->isGlobal});
			break;
		}
		found->second = digest(inputs.dump());
		statistics.validation.fingerprintsComputed++;
		return found->second;
	}
	bool diagnosticSourcesExist(const Json &record) {
		if (!sources().containsRange(record.at("range")))
			return false;
		for (const Json &note : record.at("related"))
			if (!sources().containsRange(note.at(1)))
				return false;
		for (const Json &fix : record.at("fixes"))
			if (!sources().containsRange(fix.at(1)))
				return false;
		return true;
	}
	bool reusable(const Json &record) {
		for (const Json &read : record.at("reads")) {
			auto found = sections.find(read.at(0).get<std::string>());
			if (found != sections.end() && context.deferredBodyResolution && context.deferredBodyResolution(found->second) &&
				(static_cast<ValidationRead>(read.at(1).get<int>()) == ValidationRead::References ||
				 static_cast<ValidationRead>(read.at(1).get<int>()) == ValidationRead::Binding ||
				 static_cast<ValidationRead>(read.at(1).get<int>()) == ValidationRead::DiagnosticOrder))
				continue;
			if (found == sections.end() ||
				fingerprint(
					{found->second, static_cast<ValidationRead>(read.at(1).get<int>()), read.at(2).get<std::string>()}
				) != read.at(3).get_ref<const std::string &>())
				return false;
		}
		return std::ranges::all_of(record.at("diagnostics"), [&](const Json &diagnostic) {
			return diagnosticSourcesExist(diagnostic);
		});
	}
	bool unit(Section *section, const std::function<bool()> &execute) {
		prepare();
		const std::string &path = paths.at(section);
		auto prior = records.find(path);
		if (prior != records.end() && reusable(*prior)) {
			statistics.validation.reused++;
			for (const Json &record : prior->at("diagnostics")) {
				context.addDiagnostic(restoreUnitDiagnostic(context, record, sources()));
				statistics.validation.diagnosticsRestored++;
			}
			return prior->at("success").get<bool>();
		}
		if (context.deferredBodyResolution && context.deferredBodyResolution(section)) {
			context.demandBodyResolution(section);
			for (const auto &[scope, id] : paths) {
				const Section *owner = scope;
				while (owner && owner != section)
					owner = owner->parent;
				if (owner)
					for (const auto &[name, reference] : scope->variableDefinitions)
						declarations[reference] = Json::array({id, name});
			}
		}
		std::map<Read, std::string> dependencies;
		reads = &dependencies;
		size_t first = context.diagnostics.size();
		statistics.validation.evaluated++;
		bool success = execute();
		reads = nullptr;
		Json diagnostics = Json::array(), observed = Json::array();
		for (size_t index = first; index < context.diagnostics.size(); index++)
			diagnostics.push_back(captureUnitDiagnostic(context.diagnostics[index], sources()));
		for (const auto &[read, value] : dependencies) {
			const auto &[owner, kind, name] = read;
			observed.push_back(Json::array({paths.at(owner), static_cast<int>(kind), name, value}));
		}
		records[path] = Json{{"reads", std::move(observed)}, {"diagnostics", std::move(diagnostics)}, {"success", success}};
		return success;
	}

  public:
	IncrementalValidation(
		ParseContext &context, BuildStatistics &statistics, IncrementalSourceScopes &sourceScopes, std::function<Json()> load
	)
		: context(context), statistics(statistics), sourceScopes(sourceScopes), load(std::move(load)) {}
	bool run() {
		ValidationHooks hooks;
		hooks.work = &statistics.validation;
		hooks.read = [&](Section *section, ValidationRead kind, std::string_view name) {
			requireCompilerInvariant(reads != nullptr, "capture read has no validation unit");
			Read read{section, kind, std::string(name)};
			reads->try_emplace(read, fingerprint(read));
		};
		hooks.unit = [&](Section *section, const std::function<bool()> &execute) {
			return unit(section, execute);
		};
		return validate(context, hooks);
	}
	Json snapshot() {
		loadRecords();
		return records;
	}
};
