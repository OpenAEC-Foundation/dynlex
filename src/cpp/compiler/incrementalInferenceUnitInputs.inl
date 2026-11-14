// Source inputs include the compiled signature, so a type-provider body edit
// can change a declaration even when its authored header stays the same.
class InferenceUnitInputs {
  public:
	InferenceUnitInputs(
		const ParseContext &context, const Json &projection, const std::map<const Section *, std::string> &paths,
		BuildStatistics &statistics, std::map<std::string, std::string> &fingerprints
	)
		: context(context), projection(projection), classes(classIdentityPaths(paths)), declarations(classes),
		  statistics(statistics), fingerprints(fingerprints) {
		for (const auto &[section, path] : paths)
			sections.emplace(path, section);
	}

	const std::string &fingerprint(const std::string &path) {
		auto [found, inserted] = fingerprints.try_emplace(path);
		if (!inserted)
			return found->second;
		auto start = Clock::now();
		const Section &section = *sections.at(path);
		Json input = Json::array();
		Json ambient = Json::object();
		if (!section.parent)
			for (const auto &[uri, source] : projection.at("ambient_sources").items())
				ambient[uri] = source.at("digest");
		input.push_back(
			!section.parent ? ambient
			: projection.at("bodies").contains(path) && !containsSeparateBody(section)
				? projection.at("bodies").at(path)
				: Json(digest(sourceBody(section).dump()))
		);
		for (const char *field : {"matches", "declaration_fingerprints"})
			input.push_back(projection.at("matches").contains(path) ? projection.at(field).at(path) : Json(nullptr));
		input.push_back(globalBindings(section));
		input.push_back(context.builtinSyntax.nonMessageIdentity);
		input.push_back(context.projectSyntax.nonMessageIdentity);
		input.push_back(projection.at("precedence_signature"));
		input.push_back(projection.at("precedence_rules"));
		input.push_back(declarations.signatures(section));
		Json numbers = nullptr;
		if (section.openingLine) {
			bool pending = false;
			auto rule = effectiveNumberRule(context, section.openingLine, &pending);
			requireCompilerInvariant(!pending, "inference unit read an unresolved number rule");
			if (rule)
				numbers = Json::array(
					{optional(rule->integerKind), rule->sign ? Json(static_cast<int>(*rule->sign)) : Json(nullptr),
					 optional(rule->byteWidth), rule->kindSource && rule->kindSource->absolute,
					 rule->signSource && rule->signSource->absolute, rule->widthSource && rule->widthSource->absolute}
				);
		} else {
			numbers = Json::object();
			for (const auto &[file, rules] : context.fileScopeRules)
				if (rules.numbers)
					numbers[file->uri] = Json::array({constraint(rules.numbers->properties), rules.numbers->source.absolute});
		}
		input.push_back(std::move(numbers));
		found->second = digest(input.dump());
		statistics.inferenceUnitInputsComputed++;
		statistics.inferenceUnitInputMilliseconds += elapsedMilliseconds(start);
		return found->second;
	}
	bool contains(const std::string &path) const { return sections.contains(path); }
	const Section *section(const std::string &path) const { return sections.at(path); }
	const std::string &declarationFingerprint(const std::string &path) {
		auto [found, inserted] = declarationFingerprints.try_emplace(path);
		if (inserted) {
			const Section &section = *sections.at(path);
			found->second = declarations.fingerprint(section);
		}
		return found->second;
	}

  private:
	Json globalBindings(const Section &section) const {
		std::set<std::string> names;
		auto collect = [&](auto &&self, const Section &scope) -> void {
			names.insert(scope.globalVariables.begin(), scope.globalVariables.end());
			for (const Section *child : scope.children)
				if (child->runsInEnclosingBody())
					self(self, *child);
		};
		collect(collect, section);
		const Json &resolution = projection.at("global_resolution");
		Json bindings = Json::object();
		for (const std::string &name : names) {
			Json locations = Json::array();
			for (size_t group = 0; group < 2; group++) {
				auto found = resolution.at(group).find(name);
				// Physical lines do not identify a global binding.
				locations.push_back(
					found == resolution.at(group).end() ? Json(nullptr) : Json::array({found->at(0), found->at(2)})
				);
			}
			locations.push_back(std::ranges::find(resolution.at(2), Json(name)) != resolution.at(2).end());
			bindings[name] = std::move(locations);
		}
		return bindings;
	}
	static bool containsSeparateBody(const Section &section) {
		return std::ranges::any_of(section.children, [](const Section *child) {
			return hasCodegenSectionIdentity(*child) || containsSeparateBody(*child);
		});
	}
	static Json sourceBody(const Section &section) {
		Json lines = Json::array(), children = Json::array();
		for (const CodeLine *line : section.codeLines)
			if (!line->rightTrimmedText.empty())
				lines.push_back(Json::array({line->sourceFile->uri, line->rightTrimmedText}));
		// Nested generated bodies own their source reads. Their enclosing unit
		// consumes the declaration, including body-driven parameter promotion.
		for (const Section *child : section.children)
			children.push_back(hasCodegenSectionIdentity(*child) ? sectionDeclaration(*child) : sourceBody(*child));
		return Json::array({static_cast<int>(section.type), std::move(lines), std::move(children)});
	}
	template <typename T> static Json optional(const std::optional<T> &value) { return value ? Json(*value) : Json(nullptr); }
	Json constraint(const TypeConstraint &value) const { return declarations.constraint(value); }
	const ParseContext &context;
	const Json &projection;
	ClassIdentityPaths classes;
	InferenceDeclarationInputs declarations;
	BuildStatistics &statistics;
	std::map<std::string, const Section *> sections;
	std::map<std::string, std::string> declarationFingerprints;
	std::map<std::string, std::string> &fingerprints;
};
