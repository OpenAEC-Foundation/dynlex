// Shared within one immutable inference snapshot. Rejected trials each own a
// fresh snapshot because their class tables can grow and roll back independently.
class InferenceClassSnapshot {
  public:
	explicit InferenceClassSnapshot(const InferenceValueIdentities &identities) : identities(identities) {}
	const Json &instances(const ClassDefinition *definition) {
		auto [found, inserted] = records.try_emplace(definition);
		if (inserted)
			found->second = captureClassDefinitionInstances(*definition, identities);
		return found->second;
	}

  private:
	const InferenceValueIdentities &identities;
	std::map<const ClassDefinition *, Json> records;
};
