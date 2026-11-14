// These counts depend on parsed declarations and body text, before any match
// resolves a reference or promotes an implicit parameter.
class InitialParameterCounts {
  public:
	InitialParameterCounts(ParseContext &context, const Json *previous, const Json &observations, double &milliseconds)
		: context(context), previous(previous), observations(observations), milliseconds(milliseconds) {
		context.reuseInitialVariableLikeCounts = [this](Section &section) {
			auto started = Clock::now();
			bool reused = reuse(section);
			this->milliseconds += elapsedMilliseconds(started);
			return reused;
		};
		context.recordInitialVariableLikeCounts = [this](const std::list<Section *> &sections) {
			auto started = Clock::now();
			record(sections);
			this->milliseconds += elapsedMilliseconds(started);
		};
	}
	~InitialParameterCounts() {
		context.reuseInitialVariableLikeCounts = {};
		context.recordInitialVariableLikeCounts = {};
	}

	const std::map<std::string, Section *> &sections() {
		prepare();
		return indexedSections;
	}
	const Json &bodies() {
		prepare();
		return bodyDigests;
	}
	const Json &snapshot() const { return recorded; }
	const std::string &path(const Section &section) {
		prepare();
		return paths.at(&section);
	}

  private:
	void prepare() {
		if (prepared)
			return;
		collectSections(context.mainSection, "root", indexedSections);
		for (const auto &[path, section] : indexedSections)
			paths.emplace(section, path);
		bodyDigests = definitionBodyDigests(context, indexedSections);
		if (previous && compatibleObservations(*previous, observations, context)) {
			const Json &projection = previous->at("all_function_projection");
			prior = &projection.at("initial_parameter_counts");
			priorBodies = &projection.at("matcher_projection").at("bodies");
		}
		prepared = true;
	}

	bool reuse(Section &section) {
		if (section.variableLikeCounts.empty())
			return false;
		prepare();
		const std::string &path = paths.at(&section);
		if (!prior || !prior->contains(path) || !priorBodies->contains(path) || priorBodies->at(path) != bodyDigests.at(path))
			return false;
		const Json &counts = prior->at(path);
		if (counts.size() != section.variableLikeCounts.size() ||
			!std::ranges::all_of(section.variableLikeCounts, [&](const auto &candidate) {
			return counts.contains(candidate.first);
		}))
			return false;
		for (auto &[name, count] : section.variableLikeCounts)
			count = counts.at(name).get<int>();
		return true;
	}

	void record(const std::list<Section *> &sections) {
		prepare();
		for (const Section *section : sections) {
			if (section->variableLikeCounts.empty())
				continue;
			recorded[paths.at(section)] = section->variableLikeCounts;
		}
	}

	ParseContext &context;
	const Json *previous;
	const Json &observations;
	double &milliseconds;
	const Json *prior = nullptr;
	const Json *priorBodies = nullptr;
	bool prepared = false;
	std::map<std::string, Section *> indexedSections;
	std::unordered_map<const Section *, std::string> paths;
	Json bodyDigests;
	Json recorded = Json::object();
};
