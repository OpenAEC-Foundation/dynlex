// Receiver lowering fixes the source scope tree. Data-call preparation changes
// expressions and variable initializers only, so later frontend consumers share
// this index instead of reconstructing every section identity independently.
class IncrementalSourceScopes {
	ParseContext &context;
	bool ready = false;
	std::map<std::string, Section *> scopes, compactScopes;
	std::map<const Section *, std::string> paths;

	void prepare() {
		if (ready)
			return;
		collectSections(context.mainSection, "root", scopes);
		for (const auto &[path, scope] : scopes) {
			std::string compact = digest(path);
			paths.emplace(scope, compact);
			compactScopes.emplace(compact, scope);
		}
		ready = true;
	}

  public:
	explicit IncrementalSourceScopes(ParseContext &context) : context(context) {}
	const auto &canonical() {
		prepare();
		return scopes;
	}
	const auto &compact() {
		prepare();
		return compactScopes;
	}
	const auto &identities() {
		prepare();
		return paths;
	}
};
