class IncrementalInferenceReads {
  public:
	using Keys = std::set<std::string>;
	using Scope = std::shared_ptr<const VariableStorageScope>;
	struct ValueQueries {
		std::map<std::string, Json> results;
		bool opaqueArguments = false;
	};
	struct ScopeNode {
		const Section *section;
		Keys sources;
		std::set<Scope, std::owner_less<Scope>> callees;
		Keys lookups;
		std::map<Scope, ValueQueries, std::owner_less<Scope>> values;
	};
	using ScopeNodes = std::map<Scope, ScopeNode, std::owner_less<Scope>>;

	explicit IncrementalInferenceReads(const std::map<const Section *, std::string> &sectionPaths)
		: sectionPaths(sectionPaths) {}

	void record(
		const InferenceReadOwner &owner, const lsp::SourceFile &source, std::string_view signature,
		const std::vector<CallableFunctionMatch> &matches
	) {
		std::string input = source.uri;
		input.push_back('\0');
		input.append(signature);
		std::string key = digest(input);
		auto [entry, inserted] = observations.try_emplace(key);
		if (inserted)
			entry->second = Json{
				{"source", source.uri},
				{"signature", llvm::toHex(llvm::StringRef(signature), true)},
				{"candidates", encodeCandidates(matches)}
			};
		if (owner.section) {
			bySection[sectionPaths.at(owner.section)].insert(key);
			if (owner.direct)
				ensureScope(owner.scope, owner.section).lookups.insert(key);
		}
		if (owner.constraintProbe)
			byProbe[*owner.constraintProbe].insert(std::move(key));
	}

	void recordDependency(const InferenceReadOwner &owner, const Section &dependency, const Scope &calleeScope) {
		const std::string &path = sectionPaths.at(&dependency);
		if (calleeScope)
			ensureScope(calleeScope, &dependency);
		if (owner.section) {
			if (owner.section != &dependency)
				sectionDependencies[sectionPaths.at(owner.section)].insert(path);
			if (owner.direct) {
				auto &node = ensureScope(owner.scope, owner.section);
				if (calleeScope)
					node.callees.insert(calleeScope);
				else
					node.sources.insert(path);
			}
		}
		if (owner.constraintProbe)
			probeDependencies[*owner.constraintProbe].insert(path);
	}
	const ScopeNodes &scopes() const { return scopeNodes; }
	void
	recordValue(const InferenceReadOwner &owner, const Section &dependency, const Scope &calleeScope, const Json &identity) {
		ensureScope(calleeScope, &dependency);
		if (owner.section && owner.direct) {
			auto &queries = ensureScope(owner.scope, owner.section).values[calleeScope];
			if (identity.at(0).is_null())
				queries.opaqueArguments = true;
			else
				queries.results[identity.at(0).get<std::string>()] = identity.at(1);
		}
	}

	void validate(ParseContext &context, const Json &prior) {
		if (prior.empty())
			return;
		std::unordered_map<std::string_view, const lsp::SourceFile *> sources;
		for (const auto &[_, file] : context.importedFiles)
			sources.emplace(file->uri, file);
		for (const auto &[key, lookup] : prior.items()) {
			auto source = sources.find(lookup.at("source").get_ref<const std::string &>());
			if (source == sources.end()) {
				changed.insert(key);
				continue;
			}
			Json candidates = encodeCandidates(findCallableFunctionsBySignature(
				context, llvm::fromHex(lookup.at("signature").get_ref<const std::string &>()), source->second
			));
			if (lookup.at("candidates") != candidates)
				changed.insert(key);
			validatedObservations[key] = lookup;
			validatedObservations[key]["candidates"] = std::move(candidates);
		}
		checked = prior.size();
	}

	const Keys &readsOf(const std::string &path) const {
		auto found = bySection.find(path);
		return found == bySection.end() ? empty : found->second;
	}

	const Keys &dependenciesOf(const std::string &path) const {
		auto found = sectionDependencies.find(path);
		return found == sectionDependencies.end() ? empty : found->second;
	}

	const Keys &dependenciesOfProbe(size_t index) const {
		auto found = probeDependencies.find(index);
		return found == probeDependencies.end() ? empty : found->second;
	}

	Keys readsOfProbe(size_t index, const Json *priorSchedule, bool reused) const {
		Keys keys;
		if (auto found = byProbe.find(index); found != byProbe.end())
			keys = found->second;
		if (reused) {
			requireCompilerInvariant(priorSchedule, "reused constraint probe has no saved lookup observations");
			for (const Json &key : priorSchedule->at("callable_lookups").at(index))
				keys.insert(key.get_ref<const std::string &>());
		}
		return keys;
	}

	const Json &observation(const std::string &key, const Json *prior) const {
		if (auto found = observations.find(key); found != observations.end())
			return found->second;
		if (auto found = validatedObservations.find(key); found != validatedObservations.end())
			return found->second;
		requireCompilerInvariant(prior && !changed.contains(key), "retained an invalid callable lookup");
		return prior->at(key);
	}

	size_t observedCount() const { return observations.size(); }
	size_t failedCount() const {
		return std::ranges::count_if(observations, [](const auto &entry) {
			return entry.second.at("candidates").empty();
		});
	}
	size_t checkedCount() const { return checked; }
	const Keys &changedKeys() const { return changed; }

  private:
	ScopeNode &ensureScope(const Scope &scope, const Section *section) {
		requireCompilerInvariant(section && (scope || !section->parent), "inference owner has no storage scope");
		auto [found, inserted] = scopeNodes.try_emplace(scope, ScopeNode{section, {}, {}, {}, {}});
		requireCompilerInvariant(found->second.section == section, "inference storage scope belongs to multiple sections");
		if (inserted)
			found->second.sources.insert(sectionPaths.at(section));
		return found->second;
	}
	Json encodeCandidates(const std::vector<CallableFunctionMatch> &matches) const {
		std::vector<std::tuple<std::string_view, size_t, size_t>> candidates;
		for (const CallableFunctionMatch &match : matches) {
			const auto &definitions = match.definition->section->patternDefinitions;
			auto definition = std::ranges::find(definitions, match.definition);
			requireCompilerInvariant(definition != definitions.end(), "callable lookup returned an unowned definition");
			candidates.emplace_back(
				sectionPaths.at(match.definition->section), static_cast<size_t>(definition - definitions.begin()),
				match.pathIndex
			);
		}
		std::ranges::sort(candidates);
		return candidates;
	}

	const std::map<const Section *, std::string> &sectionPaths;
	std::map<std::string, Json> observations;
	std::map<std::string, Json> validatedObservations;
	ScopeNodes scopeNodes;
	std::map<std::string, Keys> bySection;
	std::map<size_t, Keys> byProbe;
	std::map<std::string, Keys> sectionDependencies;
	std::map<size_t, Keys> probeDependencies;
	Keys changed, empty;
	size_t checked = 0;
};
