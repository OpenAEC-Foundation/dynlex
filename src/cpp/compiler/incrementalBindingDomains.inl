// Selected binding requests start after source resolution, including deferred
// section reparenting. Later source publication changes references, not scopes.
class IncrementalBindingDomains {
	ParseContext &context;
	VariableBindingWork &work;
	std::unordered_map<Section *, VariableBindingHooks::References> domains;
	bool prepared = false;

	void add(VariableReference &reference) {
		domains[variableBindingDomain(context, reference)][reference.name].push_back(&reference);
	}

  public:
	IncrementalBindingDomains(ParseContext &context, VariableBindingWork &work) : context(context), work(work) {}

	void changed(VariableReference &reference, bool added) {
		if (!prepared)
			return;
		work.domainIndexUpdates++;
		if (added) {
			add(reference);
			return;
		}
		Section *domain = variableBindingDomain(context, reference);
		auto &names = domains.at(domain);
		auto &references = names.at(reference.name);
		requireCompilerInvariant(std::erase(references, &reference) > 0, "binding domain lost a registered reference");
		if (references.empty())
			names.erase(reference.name);
		if (names.empty())
			domains.erase(domain);
	}

	VariableBindingHooks::References select(Section *domain) {
		work.domainSelections++;
		if (!prepared) {
			for (const auto &[name, references] : context.unresolvedVariableReferences) {
				(void)name;
				for (VariableReference *reference : references) {
					work.domainIndexReferenceVisits++;
					add(*reference);
				}
			}
			prepared = true;
			work.domainIndexBuilds++;
		}
		auto found = domains.find(domain);
		// Parameter-only bodies legitimately have no unresolved local references.
		return found == domains.end() ? VariableBindingHooks::References{} : found->second;
	}
};
