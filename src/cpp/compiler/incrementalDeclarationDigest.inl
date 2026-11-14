struct DeclarationProjectionWork {
	size_t unitsBuilt = 0, unitsReused = 0, recordsBuilt = 0, recordsReused = 0;
	double inputMilliseconds = 0;
};

// Audit resolved inputs once at the immutable frontend boundary. Structured
// declaration records remain cached until a consumer actually needs them.
class ResolvedDeclarationUnits {
	struct Unit {
		std::string fingerprint;
		const Json *priorRecord = nullptr;
		std::optional<Json> record;
		bool recordReused = false;
	};
	const std::map<std::string, Section *> &sections;
	DeclarationProjectionWork &work;
	std::unordered_map<const Section *, Unit> units;
	mutable std::optional<std::string> aggregate;

  public:
	ResolvedDeclarationUnits(
		const std::map<std::string, Section *> &sections, const Json *prior, DeclarationProjectionWork &work
	)
		: sections(sections), work(work) {
		auto started = Clock::now();
		for (const auto &[path, section] : sections) {
			if (section->patternDefinitions.empty() && !reusableCodegenSection(*section))
				continue;
			DeclarationFingerprintWriter writer;
			writeSectionDeclaration(writer, *section);
			Unit &unit = units[section];
			unit.fingerprint = writer.finish();
			if (prior && prior->at("declaration_fingerprints").contains(path) &&
				prior->at("declaration_fingerprints").at(path) == unit.fingerprint) {
				work.unitsReused++;
				if (auto record = prior->at("function_declarations").find(path);
					record != prior->at("function_declarations").end())
					unit.priorRecord = &*record;
			} else
				work.unitsBuilt++;
		}
		work.inputMilliseconds += elapsedMilliseconds(started);
	}

	const std::string &fingerprint(const Section &section) const { return units.at(&section).fingerprint; }
	size_t reusedRecords() const { return work.recordsReused; }
	const Json &record(const Section &section) {
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
				result[path] = unit->second.fingerprint;
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
