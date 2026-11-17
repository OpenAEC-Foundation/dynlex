#include <variant>

class DeclaredConstraintIdentities {
  public:
	explicit DeclaredConstraintIdentities(ParseContext &context) : context(context) {
		paths.emplace(context.mainSection, "root");
		for (size_t index = 0; index < context.priorConstraintQueries.size(); index++)
			requireCompilerInvariant(
				previous.emplace(context.priorConstraintQueries[index].identity, index).second,
				"saved constraint queries have duplicate identities"
			);
	}
	~DeclaredConstraintIdentities() {
		if (context.inferenceProducts)
			context.inferenceProducts->constraintIdentities(computations, hits, scopesIndexed, linesIndexed);
	}
	std::optional<size_t> priorIndex(const std::string &identity) const {
		auto found = previous.find(identity);
		return found == previous.end() ? std::nullopt : std::optional<size_t>{found->second};
	}

	const std::string &identity(const DeclaredTypeConstraintWorkItem &item) {
		Key key = item.element			   ? Key{item.element}
				  : item.variableReference ? Key{item.variableReference}
										   : Key{item.numberRuleLine};
		if (auto found = identities.find(key); found != identities.end()) {
			hits++;
			return found->second;
		}
		computations++;
		return identities.emplace(key, build(item)).first->second;
	}

  private:
	const std::string &path(const Section *section) {
		if (auto found = paths.find(section); found != paths.end())
			return found->second;
		Section *parent = section->parent;
		requireCompilerInvariant(parent, "constraint source scope has no indexed ancestor");
		const std::string &parentPath = path(parent);
		requireCompilerInvariant(indexedParents.insert(parent).second, "constraint source scope appeared after indexing");
		visitChildSectionIdentities(parent, parentPath, [&](Section *child, const std::string &childPath) {
			paths.emplace(child, childPath);
			scopesIndexed++;
		});
		return paths.at(section);
	}

	std::string build(const DeclaredTypeConstraintWorkItem &item) {
		using Json = nlohmann::json;
		requireCompilerInvariant(
			item.diagnosticRange.line && item.diagnosticRange.line->sourceFile, "type-constraint query has no source identity"
		);
		const std::string &source = item.diagnosticRange.line->sourceFile->uri;
		if (item.element)
			return Json::array({"element", item.typeConstraintName, path(item.definition->section),
								std::string(item.definition->range.subString), item.element->startPos, source})
				.dump();
		requireCompilerInvariant(item.sourceSection && item.diagnosticRange.line, "type-constraint probe has no source owner");
		const CodeLine *line = item.diagnosticRange.line;
		if (indexedLineOwners.insert(line->section).second) {
			const std::string &ownerPath = path(line->section);
			std::unordered_map<std::string, size_t> occurrences;
			for (const CodeLine *source : line->section->codeLines) {
				if (source->rightTrimmedText.empty())
					continue;
				std::string text = sectionIdentityDigest(source->rightTrimmedText);
				lines.emplace(source, Json::array({ownerPath, text, occurrences[text]++}));
				linesIndexed++;
			}
		}
		return Json::array({item.numberRuleLine ? "rule" : "variable", item.typeConstraintName, path(item.sourceSection),
							lines.at(line), item.diagnosticRange.start(), item.diagnosticRange.end(), source})
			.dump();
	}

	using Key = std::variant<const DefinitionPatternElement *, const VariableReference *, const CodeLine *>;
	ParseContext &context;
	size_t computations = 0, hits = 0, scopesIndexed = 1, linesIndexed = 0;
	std::unordered_map<Key, std::string> identities;
	std::unordered_set<const Section *> indexedParents, indexedLineOwners;
	std::unordered_map<std::string, size_t> previous;
	std::map<const Section *, std::string> paths;
	std::unordered_map<const CodeLine *, nlohmann::json> lines;
};
