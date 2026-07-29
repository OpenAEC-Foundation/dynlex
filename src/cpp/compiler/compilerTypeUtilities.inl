bool isArithmeticOperator(const std::string &name) { return isArithmeticIntrinsic(arithmeticIntrinsicKind(name)); }

bool isPointerArithmeticOperator(const std::string &name) {
	return isPointerArithmeticIntrinsic(arithmeticIntrinsicKind(name));
}

bool isComparisonOperator(const std::string &name) { return isComparisonIntrinsicKind(intrinsicKind(name)); }

bool isMathFunction(const std::string &name) {
	const IntrinsicInfo *info = findIntrinsic(name);
	return info && info->returnKind == IntrinsicReturnKind::SameAsArgs && !isArithmeticOperator(name) &&
		   intrinsicKind(name) != IntrinsicKind::Negate && intrinsicKind(name) != IntrinsicKind::Min &&
		   intrinsicKind(name) != IntrinsicKind::Max;
}

static const DefinitionPatternElement *findDefinitionParameterElementAt(
	const std::vector<DefinitionPatternElement> &elements, std::string_view parameterName, size_t startPos
) {
	for (const DefinitionPatternElement &element : elements) {
		if (element.type == PatternElement::Type::Choice) {
			for (const auto &alternative : element.alternatives) {
				if (const DefinitionPatternElement *found =
						findDefinitionParameterElementAt(alternative, parameterName, startPos))
					return found;
			}
			continue;
		}
		if ((element.type == PatternElement::Type::Variable || element.type == PatternElement::Type::Word) &&
			element.text == parameterName && element.startPos == startPos)
			return &element;
	}
	return nullptr;
}

const DefinitionPatternElement *
matchedPatternParameterElement(PatternDefinition *definition, std::string_view parameterName, size_t startPos) {
	if (!definition)
		return nullptr;
	return findDefinitionParameterElementAt(definition->patternElements, parameterName, startPos);
}

static bool equivalentSelectedPath(
	const PatternDefinition *leftDefinition, size_t leftPath, const std::vector<TypeConstraint> &leftConstraints,
	const PatternDefinition *rightDefinition, size_t rightPath, const std::vector<TypeConstraint> &rightConstraints
) {
	if (leftDefinition != rightDefinition || leftConstraints.size() != rightConstraints.size())
		return false;
	requireCompilerInvariant(
		leftPath < leftDefinition->indexedPaths.size() && rightPath < rightDefinition->indexedPaths.size(),
		"selected overload path is absent from its definition"
	);
	std::vector<std::pair<std::string, size_t>> leftParameters;
	std::vector<std::pair<std::string, size_t>> rightParameters;
	auto collectParameters = [](const auto &path, auto &parameters) {
		for (const PatternElement &element : path) {
			if (element.type == PatternElement::Type::Variable || element.type == PatternElement::Type::Word)
				parameters.emplace_back(element.text, element.startPos);
		}
	};
	collectParameters(leftDefinition->indexedPaths[leftPath], leftParameters);
	collectParameters(rightDefinition->indexedPaths[rightPath], rightParameters);
	if (leftParameters != rightParameters)
		return false;
	for (size_t index = 0; index < leftConstraints.size(); index++) {
		if (!leftConstraints[index].equivalentTo(rightConstraints[index]))
			return false;
	}
	return true;
}

std::unordered_set<std::string> collectExplicitCompileTimeParameters(
	PatternDefinition *definition, const std::vector<std::pair<std::string, Expression *>> &paramBindings, size_t pathIndex,
	const std::vector<DataType> &argTypes, const std::vector<TypeConstraint> &argumentConstraints
) {
	requireCompilerInvariant(paramBindings.size() == argTypes.size(), "pattern parameter bindings and types diverged");
	requireCompilerInvariant(
		argumentConstraints.size() == argTypes.size(), "pattern parameter constraints and types diverged"
	);
	std::unordered_set<std::string> requiredParameters;
	size_t bindingIndex = 0;
	forEachPatternParameterName(
		definition, pathIndex,
		[&](const std::string &parameterName, PatternTreeNode *, size_t startPos) {
		requireCompilerInvariant(bindingIndex < argTypes.size(), "matched pattern has more parameters than call arguments");
		const DefinitionPatternElement *parameterElement = matchedPatternParameterElement(definition, parameterName, startPos);
		requireCompilerInvariant(parameterElement != nullptr, "matched pattern parameter has no definition element");
		if (argTypes[bindingIndex].isMetaType() || parameterElement->type == PatternElement::Type::Word ||
			argumentConstraints[bindingIndex].requiresCompileTimeValue)
			requiredParameters.insert(parameterName);
		bindingIndex++;
	}
	);
	requireCompilerInvariant(bindingIndex == argTypes.size(), "matched pattern has fewer parameters than call arguments");
	return requiredParameters;
}

PatternOverloadSelection selectOverload(
	const std::vector<PatternDefinition *> &definitions, const std::vector<Expression *> & /*sortedArgs*/,
	const std::vector<PatternTreeNode *> &nodesPassed, const std::vector<DataType> &argTypes,
	const std::vector<bool> &argCompileTimeKnown, const PatternConstraintResolver &resolveConstraint
) {
	if (definitions.empty())
		return {};

	// Score each candidate: count how many type constraints match
	PatternOverloadSelection best;
	int bestScore = -1;
	std::vector<TypeConstraint> bestConstraints;

	for (auto *candidate : definitions) {
		for (size_t pathIndex : matchingPatternPathIndices(nodesPassed, candidate)) {
			int score = 0;
			bool constraintFailed = false;
			std::vector<TypeConstraint> candidateConstraints;
			candidateConstraints.reserve(argTypes.size());

			size_t argIdx = 0;
			forEachPatternParameterName(
				candidate, pathIndex,
				[&](const std::string &parameterName, PatternTreeNode *, size_t startPos) {
				if (constraintFailed || argIdx >= argTypes.size()) {
					constraintFailed = true;
					argIdx++;
					return;
				}
				const DefinitionPatternElement *parameterElement =
					matchedPatternParameterElement(candidate, parameterName, startPos);
				requireCompilerInvariant(parameterElement != nullptr, "overload parameter has no definition element");
				std::optional<ResolvedPatternConstraint> resolvedConstraint;
				if (resolveConstraint) {
					resolvedConstraint = resolveConstraint(candidate, pathIndex, argIdx);
				} else {
					if (pathIndex < candidate->signaturePaths.size() &&
						argIdx < candidate->signaturePaths[pathIndex].parameters.size()) {
						const PatternParameterSignature &signature =
							candidate->signaturePaths[pathIndex].parameters[argIdx];
						std::optional<TypeConstraint> materialized = signature.constraint.materialize({}, {});
						if (materialized) {
							bool acceptsNothing =
								signature.hasExplicitTypeConstraint &&
								materialized->accepts(DataType{DataType::Kind::Void}, false);
							resolvedConstraint = ResolvedPatternConstraint{
								std::move(*materialized), signature.constraint.structuralSpecificity(),
								signature.requiresCompileTimeValue, signature.acceptsUnresolvedType,
								acceptsNothing
							};
						}
					} else {
						resolvedConstraint = ResolvedPatternConstraint{
							parameterElement->resolvedTypeConstraint,
							parameterElement->resolvedTypeConstraint.structuralSpecificity(),
							parameterElement->type == PatternElement::Type::Word ||
								parameterElement->resolvedTypeConstraint.requiresCompileTimeValue,
							!parameterElement->resolvedTypeConstraint.isResolved(),
							!parameterElement->typeConstraintName.empty() &&
								parameterElement->resolvedTypeConstraint.isResolved() &&
								parameterElement->resolvedTypeConstraint.accepts(
									DataType{DataType::Kind::Void}, false
								)
						};
					}
				}
				if (!resolvedConstraint) {
					constraintFailed = true;
					argIdx++;
					return;
				}
				TypeConstraint parameterConstraint = std::move(resolvedConstraint->constraint);
				parameterConstraint.requiresCompileTimeValue =
					parameterConstraint.requiresCompileTimeValue || resolvedConstraint->requiresCompileTimeValue;
				const DataType &argType = argTypes[argIdx];
				if (!argType.isDeduced()) {
					bool acceptsUnset = candidate->section && candidate->section->isFlex &&
										resolvedConstraint->acceptsUnresolvedType;
					if (!acceptsUnset)
						constraintFailed = true;
					candidateConstraints.push_back(std::move(parameterConstraint));
					argIdx++;
					return;
				}
				if (parameterConstraint.isResolved()) {
					bool compileTimeKnown = argIdx < argCompileTimeKnown.size() && argCompileTimeKnown[argIdx];
					if ((argType.kind == DataType::Kind::Void && !resolvedConstraint->acceptsNothing) ||
						!parameterConstraint.accepts(argType, compileTimeKnown)) {
						constraintFailed = true;
						argIdx++;
						return;
					}
					score += resolvedConstraint->structuralSpecificity;
				}
				candidateConstraints.push_back(std::move(parameterConstraint));
				argIdx++;
			}
			);
			if (argIdx != argTypes.size())
				constraintFailed = true;
			if (constraintFailed)
				continue;
			if (score > bestScore) {
				bestScore = score;
				best = {candidate, pathIndex, false};
				bestConstraints = std::move(candidateConstraints);
			} else if (score == bestScore) {
				if (!equivalentSelectedPath(
						best.definition, best.pathIndex, bestConstraints, candidate, pathIndex, candidateConstraints
					))
					best.ambiguous = true;
			}
		}
	}

	return best;
}
