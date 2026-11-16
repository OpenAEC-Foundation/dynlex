// Matching and inference are independent producers. An encoded source owner
// still has match dependencies, even when no concrete references exist here.
class MatchedDependencyFacts {
  public:
	MatchedDependencyFacts(
		const ParseContext &context, const Json &projection, const std::map<const Section *, std::string> &paths, Json previous,
		BuildStatistics &statistics
	)
		: context(context), projection(projection), paths(paths), previous(std::move(previous)), statistics(statistics) {}

	const Json &dependencies(const Section *section) {
		const Section *owner = section;
		while (owner->patternDefinitions.empty() && owner->parent && !hasCodegenSectionIdentity(*owner))
			owner = owner->parent;
		auto [version, inserted] = versions.try_emplace(owner, nullptr);
		if (inserted) {
			const std::string &path = paths.at(owner);
			std::string products = sourceMatchInputs(projection, path);
			if (!products.empty()) {
				std::string layout;
				if (owner->parent)
					layout = projection.at("bodies").at(path).get<std::string>();
				else {
					Json scopes = Json::array();
					forEachInferenceSourceScope(*owner, [&](const Section &scope) {
						Json lines = Json::array();
						for (const CodeLine *line : scope.codeLines)
							if (!line->rightTrimmedText.empty())
								lines.push_back(Json::array({line->sourceFile->uri, line->rightTrimmedText}));
						scopes.push_back(Json::array({paths.at(&scope), std::move(lines)}));
					});
					layout = digest(scopes.dump());
				}
				version->second = digest(layout + ':' + products);
			}
		}
		const Json &input = version->second;
		const std::string &path = paths.at(section);
		if (!input.is_null() && previous.contains(path) && previous.at(path).at("input") == input) {
			current[path] = std::move(previous.at(path));
			statistics.matchedDependencyFactsReused++;
		} else {
			if (context.deferredBodyResolution && context.deferredBodyResolution(owner)) {
				requireCompilerInvariant(!input.is_null(), "deferred match producer has no version");
				context.demandBodyResolution(const_cast<Section *>(owner));
				statistics.matchedDependencyOwnersDemanded++;
			}
			std::vector<Section *> matched;
			collectMatchedSectionDependencies(section, matched);
			std::set<std::string> selected;
			for (const Section *dependency : matched)
				selected.insert(paths.at(dependency));
			current[path] = Json{{"input", input}, {"dependencies", std::move(selected)}};
			statistics.matchedDependencyFactsCaptured++;
		}
		return current.at(path).at("dependencies");
	}

	Json snapshot() { return std::move(current); }

  private:
	const ParseContext &context;
	const Json &projection;
	const std::map<const Section *, std::string> &paths;
	Json previous, current = Json::object();
	BuildStatistics &statistics;
	std::unordered_map<const Section *, Json> versions;
};
