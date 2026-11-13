class DeclaredConstraintIdentities {
  public:
	explicit DeclaredConstraintIdentities(ParseContext &context) : root(context.mainSection) {
		for (size_t index = 0; index < context.priorConstraintProbeItems.size(); index++)
			requireCompilerInvariant(
				previous.emplace(context.priorConstraintProbeItems[index], index).second,
				"saved constraint queries have duplicate identities"
			);
	}
	std::optional<size_t> priorIndex(const std::string &identity) const {
		auto found = previous.find(identity);
		return found == previous.end() ? std::nullopt : std::optional<size_t>{found->second};
	}

	std::string identity(const DeclaredTypeConstraintWorkItem &item) {
		if (paths.empty())
			visitSectionIdentities(root, "root", [&](Section *section, const std::string &path) {
				paths.emplace(section, path);
			});
		using Json = nlohmann::json;
		requireCompilerInvariant(
			item.diagnosticRange.line && item.diagnosticRange.line->sourceFile, "type-constraint query has no source identity"
		);
		const std::string &source = item.diagnosticRange.line->sourceFile->uri;
		if (item.element)
			return Json::array({"element", item.typeConstraintName, paths.at(item.definition->section),
								std::string(item.definition->range.subString), item.element->startPos, source})
				.dump();
		requireCompilerInvariant(item.sourceSection && item.diagnosticRange.line, "type-constraint probe has no source owner");
		const CodeLine *line = item.diagnosticRange.line;
		if (!lines.contains(line)) {
			for (const auto &[section, path] : paths) {
				std::unordered_map<std::string, size_t> occurrences;
				for (const CodeLine *source : section->codeLines) {
					if (source->rightTrimmedText.empty())
						continue;
					std::string text = sectionIdentityDigest(source->rightTrimmedText);
					lines.emplace(source, Json::array({path, text, occurrences[text]++}));
				}
			}
		}
		return Json::array({item.numberRuleLine ? "rule" : "variable", item.typeConstraintName, paths.at(item.sourceSection),
							lines.at(line), item.diagnosticRange.start(), item.diagnosticRange.end(), source})
			.dump();
	}

  private:
	Section *root;
	std::unordered_map<std::string, size_t> previous;
	std::map<const Section *, std::string> paths;
	std::unordered_map<const CodeLine *, nlohmann::json> lines;
};
