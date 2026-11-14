class InferenceDeclarationInputs {
  public:
	explicit InferenceDeclarationInputs(const ClassIdentityPaths &classes) : classes(classes) {}
	std::string fingerprint(const Section &section) const {
		return digest(Json::array({sectionDeclaration(section), signatures(section)}).dump());
	}
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

  private:
	const ClassIdentityPaths &classes;
};
