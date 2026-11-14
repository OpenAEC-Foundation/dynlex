// Source identities survive edits in other sections and physical line movement.
// Generated lines retain their source slices through the authored line anchors.
class InferenceBodySources {
  public:
	InferenceBodySources(ParseContext &context, const InferenceValueIdentities &identities)
		: context(context), identities(identities) {
		std::map<std::pair<std::string, std::string>, size_t> occurrences;
		for (CodeLine *line : context.codeLines) {
			if (!line->section)
				continue; // Empty preprocessing lines have no semantic owner.
			std::string owner = identities.paths.at(line->section);
			std::string text = digest(line->rightTrimmedText);
			Json id = Json::array({owner, text, occurrences[{owner, text}]++});
			lines.emplace(line, id);
			decodedLines.emplace(id.dump(), line);
			physicalLines.try_emplace(std::pair{line->sourceFile, line->sourceFileLineIndex}, line);
		}
		for (const auto &[section, path] : identities.paths) {
			for (const auto &[name, reference] : section->variableDefinitions)
				declarations.emplace(reference, Json::array({path, name}));
		}
		context.bodyBindingObservers[this] = [this](Section &owner) {
			auto visit = [&](auto &&self, Section &scope) -> void {
				for (const auto &[name, reference] : scope.variableDefinitions)
					declarations.emplace(reference, Json::array({this->identities.paths.at(&scope), name}));
				for (Section *child : scope.children)
					self(self, *child);
			};
			visit(visit, owner);
		};
	}
	InferenceBodySources(const InferenceBodySources &) = delete;
	InferenceBodySources &operator=(const InferenceBodySources &) = delete;
	~InferenceBodySources() { context.bodyBindingObservers.erase(this); }

	Json section(const Section *value) const { return value ? Json(identities.paths.at(value)) : Json(nullptr); }
	Section *section(const Json &value) const {
		return value.is_null() ? nullptr : identities.sections.at(value.get<std::string>());
	}
	Json definition(const PatternDefinition *value) const {
		if (!value)
			return nullptr;
		const auto &definitions = value->section->patternDefinitions;
		auto found = std::ranges::find(definitions, value);
		requireCompilerInvariant(found != definitions.end(), "inferred body has an unowned definition");
		return Json::array({section(value->section), static_cast<size_t>(found - definitions.begin())});
	}
	PatternDefinition *definition(const Json &value) const {
		return value.is_null() ? nullptr : section(value.at(0))->patternDefinitions.at(value.at(1).get<size_t>());
	}
	Json declaration(const VariableReference *value) const {
		auto found = declarations.find(value);
		return found == declarations.end() ? Json(nullptr) : found->second;
	}
	VariableReference *declaration(const Json &value) const {
		Section *owner = section(value.at(0));
		if (context.demandBodyResolution)
			context.demandBodyResolution(owner);
		return owner->variableDefinitions.at(value.at(1).get<std::string>());
	}

	Json line(const CodeLine *value) const {
		if (!value)
			return nullptr;
		if (auto found = lines.find(value); found != lines.end())
			return Json{{"source", found->second}};
		Json slices = Json::array();
		for (const SourceSlice &slice : value->sourceSlices) {
			auto anchor = physicalLines.find({slice.sourceFile, slice.sourceFileLineIndex});
			requireCompilerInvariant(anchor != physicalLines.end(), "generated body line has no authored source anchor");
			slices.push_back(
				Json::array({slice.transformedStart, slice.transformedEnd, lines.at(anchor->second), slice.sourceColumnStart})
			);
		}
		auto origin = physicalLines.find({value->sourceFile, value->sourceFileLineIndex});
		requireCompilerInvariant(origin != physicalLines.end(), "generated body line has no source origin");
		return Json{
			{"text", value->fullText},
			{"section", section(value->section)},
			{"opening", section(value->sectionOpening)},
			{"origin", lines.at(origin->second)},
			{"slices", std::move(slices)},
			{"trim", Json::array({offset(value, value->rightTrimmedText), value->rightTrimmedText.size()})},
			{"pattern", Json::array({offset(value, value->patternText), value->patternText.size()})}
		};
	}
	CodeLine *line(const Json &value) {
		if (value.is_null())
			return nullptr;
		if (value.contains("source"))
			return decodedLines.at(value.at("source").dump());
		std::string key = value.dump();
		if (auto found = generatedLines.find(key); found != generatedLines.end())
			return found->second;
		std::vector<SourceSlice> slices;
		for (const Json &slice : value.at("slices")) {
			CodeLine *anchor = decodedLines.at(slice.at(2).dump());
			slices.push_back(
				{slice.at(0).get<int>(), slice.at(1).get<int>(), anchor->sourceFile, anchor->sourceFileLineIndex,
				 slice.at(3).get<int>()}
			);
		}
		Section *owner = section(value.at("section"));
		CodeLine *origin = decodedLines.at(value.at("origin").dump());
		CodeLine *result = context.createCodeLine(
			origin->sourceFile, origin->sourceFileLineIndex, value.at("text").get<std::string>(), std::move(slices)
		);
		result->section = owner;
		result->sectionOpening = section(value.at("opening"));
		result->rightTrimmedText =
			result->fullText.substr(value.at("trim").at(0).get<size_t>(), value.at("trim").at(1).get<size_t>());
		result->patternText =
			result->fullText.substr(value.at("pattern").at(0).get<size_t>(), value.at("pattern").at(1).get<size_t>());
		generatedLines.emplace(std::move(key), result);
		return result;
	}
	Json range(const Range &value) const {
		return value.line ? Json::array({line(value.line), value.start(), value.end()}) : Json(nullptr);
	}
	Range range(const Json &value) {
		return value.is_null() ? Range{} : Range(line(value.at(0)), value.at(1).get<int>(), value.at(2).get<int>());
	}
	bool containsRange(const Json &value) const {
		if (value.is_null())
			return true;
		const Json &line = value.at(0);
		if (line.contains("source"))
			return decodedLines.contains(line.at("source").dump());
		for (const char *field : {"section", "opening"})
			if (!line.at(field).is_null() && !identities.sections.contains(line.at(field).get<std::string>()))
				return false;
		if (!decodedLines.contains(line.at("origin").dump()))
			return false;
		for (const Json &slice : line.at("slices"))
			if (!decodedLines.contains(slice.at(2).dump()))
				return false;
		return true;
	}

	Json node(const PatternTreeNode *value) const {
		if (!value)
			return nullptr;
		std::vector<const PatternTreeNode *> edges;
		while (value->parentNode) {
			edges.push_back(value);
			value = value->parentNode;
		}
		size_t root = 0;
		while (root < std::size(context.patternTrees) && context.patternTrees[root] != value)
			root++;
		requireCompilerInvariant(root < std::size(context.patternTrees), "inferred match has an unowned tree node");
		Json path = Json::array();
		for (auto edge = edges.rbegin(); edge != edges.rend(); ++edge)
			path.push_back(Json::array({static_cast<int>((*edge)->type), (*edge)->text, (*edge)->isArgumentPack}));
		return Json::array({root, std::move(path)});
	}
	PatternTreeNode *node(const Json &value) const {
		if (value.is_null())
			return nullptr;
		PatternTreeNode *result = context.patternTrees[value.at(0).get<size_t>()];
		for (const Json &edge : value.at(1)) {
			if (static_cast<PatternElement::Type>(edge.at(0).get<int>()) == PatternElement::Variable)
				result = edge.at(2).get<bool>() ? result->argumentPackChild : result->argumentChild;
			else
				result = result->literalChildren.at(edge.at(1).get<std::string>());
			requireCompilerInvariant(result, "validated inferred body lost a pattern node");
		}
		return result;
	}

  private:
	static size_t offset(const CodeLine *line, std::string_view span) {
		return span.empty() ? 0 : static_cast<size_t>(span.data() - line->fullText.data());
	}
	ParseContext &context;
	const InferenceValueIdentities &identities;
	std::unordered_map<const CodeLine *, Json> lines;
	std::map<std::string, CodeLine *> decodedLines, generatedLines;
	std::map<std::pair<const lsp::SourceFile *, int>, CodeLine *> physicalLines;
	std::unordered_map<const VariableReference *, Json> declarations;
};
