// Source inputs include the compiled signature, so a type-provider body edit
// can change a declaration even when its authored header stays the same.
class InferenceUnitInputs {
  public:
	InferenceUnitInputs(
		const ParseContext &context, const Json &projection, const std::map<const Section *, std::string> &paths,
		BuildStatistics &statistics, std::map<std::string, std::string> &fingerprints
	)
		: context(context), projection(projection), classes(classIdentityPaths(paths)), statistics(statistics),
		  fingerprints(fingerprints) {
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
		for (const char *field : {"matches", "function_declarations"})
			input.push_back(projection.at(field).contains(path) ? projection.at(field).at(path) : Json(nullptr));
		if (!hasNoGlobalAccess(&section)) {
			Json bindings = projection.at("global_resolution");
			for (size_t group = 0; group < 2; group++)
				for (auto &[name, location] : bindings.at(group).items())
					location.erase(location.begin() + 1); // Physical lines do not identify a global binding.
			input.push_back(std::move(bindings));
		}
		input.push_back(context.builtinSyntax.nonMessageIdentity);
		input.push_back(context.projectSyntax.nonMessageIdentity);
		input.push_back(projection.at("precedence_signature"));
		input.push_back(projection.at("precedence_rules"));
		input.push_back(signatures(section));
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
			found->second = digest(Json::array({sectionDeclaration(section), signatures(section)}).dump());
		}
		return found->second;
	}

  private:
	Json signatures(const Section &section) const {
		Json result = Json::array();
		for (const PatternDefinition *definition : section.patternDefinitions) {
			Json paths = Json::array();
			for (const PatternPathSignature &signature : definition->signaturePaths) {
				Json parameters = Json::array();
				for (const PatternParameterSignature &parameter : signature.parameters)
					parameters.push_back(
						Json::array(
							{parameter.elementStartPos, constraintTemplate(parameter.constraint),
							 parameter.requiresCompileTimeValue, parameter.acceptsUnresolvedType,
							 parameter.hasExplicitTypeConstraint}
						)
					);
				paths.push_back(std::move(parameters));
			}
			result.push_back(std::move(paths));
		}
		return result;
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
	Json constraint(const TypeConstraint &value) const {
		auto encoded = encodePortableConstraint(value, classes);
		requireCompilerInvariant(encoded.has_value(), "compiled signature has no portable constraint identity");
		return std::move(*encoded);
	}
	static Json integerTerm(const ConstraintIntegerTerm &term) {
		return Json::array(
			{static_cast<int>(term.kind), term.constant, term.sourceArgumentIndex, term.dimension,
			 term.left ? integerTerm(*term.left) : Json(nullptr), term.right ? integerTerm(*term.right) : Json(nullptr)}
		);
	}
	Json constraintTemplate(const TypeConstraintTemplate &value) const {
		Json result = Json::array({constraint(value.constantPart)});
		result.push_back(
			value.rootProjection ? Json::array({value.rootProjection->sourceArgumentIndex, value.rootProjection->elementDepth})
								 : Json(nullptr)
		);
		for (const auto *term :
			 {&value.numericSize, &value.pointerDepth, &value.arraySize, &value.matrixRows, &value.matrixColumns})
			result.push_back(*term ? integerTerm(**term) : Json(nullptr));
		result.push_back(value.elementConstraint ? constraintTemplate(*value.elementConstraint) : Json(nullptr));
		return result;
	}
	const ParseContext &context;
	const Json &projection;
	ClassIdentityPaths classes;
	BuildStatistics &statistics;
	std::map<std::string, const Section *> sections;
	std::map<std::string, std::string> declarationFingerprints;
	std::map<std::string, std::string> &fingerprints;
};
