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
		if (element.type == PatternElement::Type::Variable && element.text == parameterName && element.startPos == startPos)
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
			if (element.type == PatternElement::Type::Variable)
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
	requireCompilerInvariant(argumentConstraints.size() == argTypes.size(), "pattern parameter constraints and types diverged");
	std::unordered_set<std::string> requiredParameters;
	size_t bindingIndex = 0;
	forEachPatternParameterName(
		definition, pathIndex,
		[&](const std::string &parameterName, PatternTreeNode *, size_t startPos) {
		requireCompilerInvariant(bindingIndex < argTypes.size(), "matched pattern has more parameters than call arguments");
		const DefinitionPatternElement *parameterElement = matchedPatternParameterElement(definition, parameterName, startPos);
		requireCompilerInvariant(parameterElement != nullptr, "matched pattern parameter has no definition element");
		if (argTypes[bindingIndex].isMetaType() || argumentConstraints[bindingIndex].requiresCompileTimeValue)
			requiredParameters.insert(parameterName);
		bindingIndex++;
	}
	);
	requireCompilerInvariant(bindingIndex == argTypes.size(), "matched pattern has fewer parameters than call arguments");
	return requiredParameters;
}

ResolvedPatternConstraint
resolveInitialPatternConstraint(PatternDefinition *definition, size_t pathIndex, size_t argumentIndex) {
	requireCompilerInvariant(definition != nullptr, "initial pattern constraint resolution requires a definition");
	requireCompilerInvariant(
		pathIndex < definition->indexedPaths.size(), "initial pattern constraint path is absent from its definition"
	);
	const DefinitionPatternElement *parameterElement = nullptr;
	size_t currentArgument = 0;
	forEachPatternParameterName(definition, pathIndex, [&](const std::string &name, PatternTreeNode *, size_t startPos) {
		if (currentArgument++ == argumentIndex)
			parameterElement = matchedPatternParameterElement(definition, name, startPos);
	});
	requireCompilerInvariant(parameterElement != nullptr, "initial pattern constraint argument is absent from its path");
	const bool untypedParameter = parameterElement->typeConstraintName.empty();
	requireCompilerInvariant(
		parameterElement->resolvedTypeConstraint.isResolved() || untypedParameter,
		"initial pattern constraint resolution encountered an unresolved dependency"
	);
	TypeConstraint constraint = parameterElement->resolvedTypeConstraint.isResolved() ? parameterElement->resolvedTypeConstraint
																					  : TypeConstraint::any();
	const bool acceptsNothing = !parameterElement->typeConstraintName.empty() && constraint.explicitlyAcceptsNothing();
	return ResolvedPatternConstraint{
		std::move(constraint), parameterElement->resolvedTypeConstraint.requiresCompileTimeValue, untypedParameter,
		acceptsNothing
	};
}

std::optional<ResolvedPatternConstraint> resolveCompiledPatternConstraint(
	PatternDefinition *definition, size_t pathIndex, size_t argumentIndex, const std::vector<DataType> &argumentTypes,
	const std::vector<CompileTimeValue> &argumentValues
) {
	requireCompilerInvariant(definition != nullptr, "pattern constraint resolution requires a definition");
	requireCompilerInvariant(
		pathIndex < definition->signaturePaths.size(), "pattern signature path is absent during constraint resolution"
	);
	const PatternPathSignature &path = definition->signaturePaths[pathIndex];
	requireCompilerInvariant(
		argumentIndex < path.parameters.size(), "pattern signature argument is absent during constraint resolution"
	);
	const PatternParameterSignature &signature = path.parameters[argumentIndex];
	std::optional<TypeConstraint> materialized = signature.constraint.materialize(argumentTypes, argumentValues);
	if (!materialized)
		return std::nullopt;
	const bool acceptsNothing = signature.hasExplicitTypeConstraint && materialized->explicitlyAcceptsNothing();
	return ResolvedPatternConstraint{
		std::move(*materialized), signature.requiresCompileTimeValue, signature.acceptsUnresolvedType, acceptsNothing
	};
}

PatternOverloadSelection selectOverload(
	const std::vector<PatternDefinition *> &definitions, const std::vector<Expression *> & /*sortedArgs*/,
	const std::vector<PatternTreeNode *> &nodesPassed, const std::vector<DataType> &argTypes,
	const std::vector<bool> &argCompileTimeKnown, const PatternConstraintResolver &resolveConstraint
) {
	if (definitions.empty())
		return {};
	requireCompilerInvariant(static_cast<bool>(resolveConstraint), "overload selection requires a constraint resolver");

	struct ViableOverload {
		PatternDefinition *definition;
		size_t pathIndex;
		std::vector<TypeConstraint> constraints;
	};
	std::vector<ViableOverload> viableOverloads;

	for (auto *candidate : definitions) {
		for (size_t pathIndex : matchingPatternPathIndices(nodesPassed, candidate)) {
			bool constraintFailed = false;
			std::vector<TypeConstraint> candidateConstraints;
			candidateConstraints.reserve(argTypes.size());

			size_t argIdx = 0;
			forEachPatternParameterName(candidate, pathIndex, [&](const std::string &, PatternTreeNode *, size_t) {
				if (constraintFailed || argIdx >= argTypes.size()) {
					constraintFailed = true;
					argIdx++;
					return;
				}
				std::optional<ResolvedPatternConstraint> resolvedConstraint = resolveConstraint(candidate, pathIndex, argIdx);
				if (!resolvedConstraint) {
					constraintFailed = true;
					argIdx++;
					return;
				}
				TypeConstraint parameterConstraint = resolvedConstraint->effectiveConstraint();
				const DataType &argType = argTypes[argIdx];
				if (!argType.isDeduced()) {
					bool acceptsUnset =
						candidate->section && candidate->section->isFlex && resolvedConstraint->acceptsUnresolvedType;
					if (!acceptsUnset)
						constraintFailed = true;
					candidateConstraints.push_back(std::move(parameterConstraint));
					argIdx++;
					return;
				}
				if (parameterConstraint.isResolved()) {
					bool compileTimeKnown = argIdx < argCompileTimeKnown.size() && argCompileTimeKnown[argIdx];
					if (!resolvedConstraint->accepts(argType, compileTimeKnown)) {
						constraintFailed = true;
						argIdx++;
						return;
					}
				}
				candidateConstraints.push_back(std::move(parameterConstraint));
				argIdx++;
			});
			if (argIdx != argTypes.size())
				constraintFailed = true;
			if (constraintFailed)
				continue;
			viableOverloads.push_back({candidate, pathIndex, std::move(candidateConstraints)});
		}
	}

	if (viableOverloads.empty())
		return {};

	std::vector<size_t> maximalOverloads;
	for (size_t candidateIndex = 0; candidateIndex < viableOverloads.size(); candidateIndex++) {
		bool dominated = false;
		for (size_t otherIndex = 0; otherIndex < viableOverloads.size(); otherIndex++) {
			if (candidateIndex == otherIndex)
				continue;
			if (compareConstraintSpecificity(
					viableOverloads[candidateIndex].constraints, viableOverloads[otherIndex].constraints
				) == ConstraintSpecificity::RightMoreSpecific) {
				dominated = true;
				break;
			}
		}
		if (!dominated)
			maximalOverloads.push_back(candidateIndex);
	}
	requireCompilerInvariant(!maximalOverloads.empty(), "viable overload domains have no maximal candidate");

	const ViableOverload &selected = viableOverloads[maximalOverloads.front()];
	PatternOverloadSelection result{selected.definition, selected.pathIndex, false};
	for (size_t maximalIndex : maximalOverloads) {
		const ViableOverload &other = viableOverloads[maximalIndex];
		if (!equivalentSelectedPath(
				selected.definition, selected.pathIndex, selected.constraints, other.definition, other.pathIndex,
				other.constraints
			)) {
			result.ambiguous = true;
			break;
		}
	}
	return result;
}
