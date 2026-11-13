// Shared within one immutable inference snapshot. Rejected trials each own a
// fresh snapshot because their class tables can grow and roll back independently.
class InferenceClassSnapshot {
  public:
	InferenceClassSnapshot(const InferenceValueIdentities &identities, std::function<Json(const Json &)> save)
		: identities(identities), save(std::move(save)) {}
	const Json &instances(const ClassDefinition *definition) { return record(definition).state; }
	const Json &reference(const ClassDefinition *definition) { return record(definition).reference; }

  private:
	struct Record {
		Json state;
		Json reference;
	};
	const Record &record(const ClassDefinition *definition) {
		auto [found, inserted] = records.try_emplace(definition);
		if (inserted) {
			found->second.state = captureClassDefinitionInstances(*definition, identities);
			found->second.reference = save(found->second.state);
		}
		return found->second;
	}
	const InferenceValueIdentities &identities;
	std::function<Json(const Json &)> save;
	std::map<const ClassDefinition *, Record> records;
};
