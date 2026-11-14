// Declaration discovery uses the source header throughout resolution. A matched
// expression acquires captured arguments, so it is not a source snapshot.
class DataDeclarationNameIndex {
	struct Header {
		Expression expression;
		PatternReference reference;
		explicit Header(const PatternReference &source)
			: expression(*source.expression), reference(&expression, source.patternType) {
			reference.sourceRange = source.sourceRange;
			reference.pattern = source.pattern;
			reference.patternElements = source.patternElements;
			reference.matchingScope = source.matchingScope;
		}
	};
	using Names = std::unordered_map<std::string, Range>;
	struct Scope {
		Names ordinary;
		Names published;
	};
	std::unordered_set<PatternReference *> seen;
	std::vector<std::unique_ptr<Header>> headers;
	std::unordered_map<Section *, Scope> scopes;

	static bool sameRange(const Range &left, const Range &right) {
		return left.line == right.line && left.start() == right.start() && left.end() == right.end();
	}
	static bool sameNames(const Names &left, const Names &right) {
		if (left.size() != right.size())
			return false;
		for (const auto &[name, range] : left) {
			auto found = right.find(name);
			if (found == right.end() || !sameRange(range, found->second))
				return false;
		}
		return true;
	}

  public:
	void add(const std::list<PatternReference *> &references) {
		for (PatternReference *reference : references) {
			if (reference->patternType != SectionType::Section || !reference->range().line->sectionOpening ||
				!seen.insert(reference).second)
				continue;
			requireCompilerInvariant(!reference->resolved, "declaration discovery received a new resolved header");
			headers.push_back(std::make_unique<Header>(*reference));
		}
	}

	bool refresh(ParseContext &context, std::unordered_set<Section *> &changedScopes) {
		std::unordered_map<Section *, Names> discovered;
		for (const auto &header : headers) {
			PatternReference &reference = header->reference;
			std::unique_ptr<PatternMatch> match(context.match(&reference));
			if (!match)
				continue;
			for (PatternDefinition *definition : match->matchingDefinitions) {
				if (!definition->section->dataSection || definition->section->dataSection->declarationParameter.empty())
					continue;
				if (const VariableMatch *variable = dataSectionDeclaredVariable(*match, *definition)) {
					Range range(
						reference.range().line, reference.range().start() + variable->lineStartPos,
						reference.range().start() + variable->lineEndPos
					);
					auto &names = discovered[reference.range().section()];
					auto [entry, inserted] = names.emplace(variable->name, range);
					if (!inserted && std::pair(range.line->mergedLineIndex, range.start()) <
										 std::pair(entry->second.line->mergedLineIndex, entry->second.start()))
						entry->second = range;
				}
			}
		}
		for (const auto &[section, names] : discovered) {
			(void)names;
			scopes.try_emplace(section);
		}
		bool changed = false;
		for (auto &[section, scope] : scopes) {
			Names ordinary = section->explicitVariableDeclarations;
			// Remove our previous contribution, preserving explicit captures
			// that existed before it or were introduced by later resolution.
			for (const auto &[name, range] : scope.published) {
				auto current = ordinary.find(name);
				if (current == ordinary.end() || !sameRange(current->second, range))
					continue;
				auto original = scope.ordinary.find(name);
				if (original == scope.ordinary.end())
					ordinary.erase(current);
				else
					current->second = original->second;
			}
			scope.ordinary = std::move(ordinary);
			Names &names = discovered[section];
			if (!sameNames(scope.published, names)) {
				changed = true;
				changedScopes.insert(section);
			}
			scope.published = names;
			section->explicitVariableDeclarations = scope.ordinary;
			section->dataVariableDeclarations.clear();
			for (const auto &[name, range] : names) {
				section->dataVariableDeclarations.insert(name);
				section->registerExplicitVariableName(name, range);
			}
		}
		return changed;
	}
};
