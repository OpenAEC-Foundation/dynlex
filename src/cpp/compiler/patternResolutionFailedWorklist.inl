// A failed reference waits on the trie edges and endpoints observed by its
// matcher. Tree insertion wakes only references whose recorded condition
// actually changed; the current matcher still makes the final decision.
class FailedReferenceWorklist {
  public:
	explicit FailedReferenceWorklist(FailedMatchDependencies &dependencies) : dependencies(dependencies) {}

	void park(PatternReference *reference) {
		const auto &failed = dependencies.at(reference);
		if (seen.insert(reference).second)
			blocked.push_back(reference);
		waiting.insert(reference);
		for (const MatchDependency &dependency : failed)
			watchers[{dependency.node, dependency.kind, dependency.literal}].push_back(reference);
	}

	void notifyAddedDefinition(const PatternDefinition &definition) {
		requireCompilerInvariant(
			definition.indexedTree && definition.indexedPaths.size() == definition.indexedNodePaths.size(),
			"failed reference notification requires an indexed definition"
		);
		for (size_t pathIndex = 0; pathIndex < definition.indexedPaths.size(); pathIndex++) {
			PatternTreeNode *parent = definition.indexedTree;
			const auto &path = definition.indexedPaths[pathIndex];
			const auto &nodes = definition.indexedNodePaths[pathIndex];
			requireCompilerInvariant(path.size() == nodes.size(), "indexed pattern path has inconsistent nodes");
			for (size_t index = 0; index < path.size(); index++) {
				const PatternElement &element = path[index];
				MatchDependency::Kind kind = element.type == PatternElement::Type::Variable
												 ? (element.isArgumentPack ? MatchDependency::Kind::ArgumentPackChild
																		   : MatchDependency::Kind::ArgumentChild)
												 : MatchDependency::Kind::LiteralChild;
				notify({parent, kind, kind == MatchDependency::Kind::LiteralChild ? element.text : ""});
				parent = nodes[index];
			}
		}
		for (PatternTreeNode *endpoint : definition.endNodes)
			notify({endpoint, MatchDependency::Kind::Endpoint, ""});
	}

	void setUnmatchedVariablesAllowed(bool allowed) {
		if (unmatchedVariablesAllowed == allowed)
			return;
		unmatchedVariablesAllowed = allowed;
		if (!allowed)
			return;
		// Reclassification can reopen definition resolution. References parked
		// while names were provisional become eligible each time it completes.
		for (PatternReference *reference : blocked) {
			if (!waiting.contains(reference) || reference->patternElements.size() != 1 ||
				reference->patternElements.front().type != PatternElement::Type::VariableLike)
				continue;
			wake(reference);
		}
	}

	void wakeNewExplicitVariables() {
		for (PatternReference *reference : blocked) {
			if (waiting.contains(reference) && findVisibleExplicitWholeVariableName(reference))
				wake(reference);
		}
	}

	bool drain(std::list<PatternReference *> &references) {
		bool hadPending = !pending.empty();
		std::sort(pending.begin(), pending.end(), referenceComesBefore);
		for (PatternReference *reference : pending)
			references.push_back(reference);
		pending.clear();
		return hadPending;
	}

	bool hasWaiting() const { return !waiting.empty(); }
	const std::vector<PatternReference *> &blockedReferences() const { return blocked; }
	bool isWaiting(PatternReference *reference) const { return waiting.contains(reference); }

  private:
	struct WatchKey {
		const PatternTreeNode *node;
		MatchDependency::Kind kind;
		std::string literal;
		bool operator==(const WatchKey &) const = default;
	};
	struct WatchHash {
		size_t operator()(const WatchKey &key) const {
			size_t hash = std::hash<const PatternTreeNode *>{}(key.node);
			hash ^= std::hash<int>{}(static_cast<int>(key.kind)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
			hash ^= std::hash<std::string>{}(key.literal) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
			return hash;
		}
	};

	void wake(PatternReference *reference) {
		if (waiting.erase(reference))
			pending.push_back(reference);
	}
	void notify(const WatchKey &key) {
		auto found = watchers.find(key);
		if (found == watchers.end())
			return;
		for (PatternReference *reference : found->second) {
			if (waiting.contains(reference) && matchDependenciesChanged(dependencies.at(reference)))
				wake(reference);
		}
	}

	FailedMatchDependencies &dependencies;
	std::unordered_map<WatchKey, std::vector<PatternReference *>, WatchHash> watchers;
	std::unordered_set<PatternReference *> seen;
	std::unordered_set<PatternReference *> waiting;
	std::vector<PatternReference *> blocked;
	std::vector<PatternReference *> pending;
	bool unmatchedVariablesAllowed = false;
};
