struct DeclarationProjectionWork {
	size_t inputsChecked = 0, unitsBuilt = 0, unitsReused = 0, recordsBuilt = 0, recordsReused = 0;
	double inputMilliseconds = 0;
};

// Audit resolved inputs once at the immutable frontend boundary. Structured
// declaration records remain cached until a consumer actually needs them.
class ResolvedDeclarationUnits {
	struct Unit {
		std::string fingerprint;
		const std::string *priorFingerprint = nullptr;
		const Json *priorRecord = nullptr;
		std::optional<Json> record;
		bool ready = false, recordReused = false;
	};
	const std::map<std::string, Section *> &sections;
	const Json *prior;
	DeclarationProjectionWork &work;
	mutable std::unordered_map<const Section *, Unit> units;
	mutable std::optional<std::string> aggregate;

	void audit(const Section &section) const {
		Unit &unit = units.at(&section);
		if (unit.ready)
			return;
		auto started = Clock::now();
		DeclarationFingerprintWriter writer;
		writeSectionDeclaration(writer, section);
		unit.fingerprint = writer.finish();
		unit.ready = true;
		work.inputsChecked++;
		if (unit.priorFingerprint && unit.fingerprint == *unit.priorFingerprint)
			work.unitsReused++;
		else {
			work.unitsBuilt++;
			unit.priorRecord = nullptr;
		}
		work.inputMilliseconds += elapsedMilliseconds(started);
	}

  public:
	ResolvedDeclarationUnits(
		const std::map<std::string, Section *> &sections, const Json *prior, DeclarationProjectionWork &work
	)
		: sections(sections), prior(prior), work(work) {
		for (const auto &[path, section] : sections) {
			if (section->patternDefinitions.empty() && !reusableCodegenSection(*section))
				continue;
			Unit &unit = units[section];
			if (prior && prior->at("declaration_fingerprints").contains(path)) {
				unit.priorFingerprint = &prior->at("declaration_fingerprints").at(path).get_ref<const std::string &>();
				if (auto record = prior->at("function_declarations").find(path);
					record != prior->at("function_declarations").end())
					unit.priorRecord = &*record;
			}
		}
	}

	const std::string &fingerprint(const Section &section) const {
		audit(section);
		return units.at(&section).fingerprint;
	}

	// Unchanged sources, matcher interfaces and bindings prove that unchanged
	// bodies retain their resolved declaration. Changed bodies still audit their
	// actual signature before that proof can stop propagation.
	bool retainSourceUnits(
		const Json &projection, const Json &bodies, const Json &rules, const Json &globals, bool stableSources,
		std::string_view initialTree, std::string_view finalTree
	) {
		if (prior != &projection || !stableSources || units.size() != projection.at("declaration_fingerprints").size() ||
			initialTree != projection.at("initial_matcher_tree").get_ref<const std::string &>() ||
			finalTree != projection.at("final_matcher_tree").get_ref<const std::string &>() ||
			rules != projection.at("scope_rule_bodies") || globals != projection.at("global_resolution") ||
			bodies.size() != projection.at("bodies").size() || !std::ranges::all_of(units, [](const auto &entry) {
			return entry.second.priorFingerprint != nullptr;
		}))
			return false;
		for (const auto &[path, body] : bodies.items()) {
			auto old = projection.at("bodies").find(path);
			if (old == projection.at("bodies").end())
				return false;
			if (body == *old)
				continue;
			const Section &section = *sections.at(path);
			if (!reusableCodegenSection(section) || rules.contains(path) ||
				fingerprint(section) != *units.at(&section).priorFingerprint)
				return false;
		}
		for (auto &[_, unit] : units)
			if (!unit.ready) {
				unit.fingerprint = *unit.priorFingerprint;
				unit.ready = true;
				work.unitsReused++;
			}
		return true;
	}
	size_t reusedRecords() const { return work.recordsReused; }
	const Json &record(const Section &section) {
		audit(section);
		Unit &unit = units.at(&section);
		if (unit.priorRecord) {
			if (!std::exchange(unit.recordReused, true))
				work.recordsReused++;
			return *unit.priorRecord;
		}
		if (!unit.record) {
			unit.record = sectionDeclaration(section);
			work.recordsBuilt++;
		}
		return *unit.record;
	}
	Json fingerprints() const {
		Json result = Json::object();
		for (const auto &[path, section] : sections)
			if (auto unit = units.find(section); unit != units.end())
				result[path] = fingerprint(*section);
		return result;
	}
	std::string digest(const std::unordered_set<const Section *> &excluded = {}) const {
		if (excluded.empty() && aggregate)
			return *aggregate;
		DeclarationFingerprintWriter writer;
		size_t count = std::ranges::count_if(sections, [&](const auto &entry) {
			return units.contains(entry.second) && !excluded.contains(entry.second);
		});
		writer.array(count);
		for (const auto &[path, section] : sections) {
			if (!units.contains(section) || excluded.contains(section))
				continue;
			writer.array(2);
			writer.value(path);
			writer.value(fingerprint(*section));
			writer.end();
		}
		writer.end();
		std::string result = writer.finish();
		if (excluded.empty())
			aggregate = result;
		return result;
	}
};
