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

bool patternParameterRequiresCompileTimeValue(const DefinitionPatternElement &parameterElement, const DataType &argType) {
	if (argType.isMetaType())
		return true;
	if (parameterElement.type == PatternElement::Type::Word)
		return true;
	return parameterElement.resolvedTypeConstraint.isResolved() &&
		   parameterElement.resolvedTypeConstraint.requiresCompileTimeValue;
}

std::unordered_set<std::string> collectExplicitCompileTimeParameters(
	PatternDefinition *definition, const std::vector<std::pair<std::string, Expression *>> &paramBindings, size_t pathIndex,
	const std::vector<DataType> &argTypes
) {
	requireCompilerInvariant(paramBindings.size() == argTypes.size(), "pattern parameter bindings and types diverged");
	std::unordered_set<std::string> requiredParameters;
	size_t bindingIndex = 0;
	forEachPatternParameterName(
		definition, pathIndex,
		[&](const std::string &parameterName, PatternTreeNode *, size_t startPos) {
		requireCompilerInvariant(bindingIndex < argTypes.size(), "matched pattern has more parameters than call arguments");
		const DefinitionPatternElement *parameterElement = matchedPatternParameterElement(definition, parameterName, startPos);
		requireCompilerInvariant(parameterElement != nullptr, "matched pattern parameter has no definition element");
		if (patternParameterRequiresCompileTimeValue(*parameterElement, argTypes[bindingIndex]))
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
	const std::vector<bool> &argCompileTimeKnown
) {
	if (definitions.empty())
		return {};

	// Score each candidate: count how many type constraints match
	PatternOverloadSelection best;
	int bestScore = -1;

	for (auto *candidate : definitions) {
		for (size_t pathIndex : matchingPatternPathIndices(nodesPassed, candidate)) {
			int score = 0;
			bool constraintFailed = false;

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
				const DataType &argType = argTypes[argIdx];
				if (!argType.isDeduced()) {
					bool acceptsUnset = candidate->section && candidate->section->isFlex &&
										!parameterElement->resolvedTypeConstraint.isResolved();
					if (!acceptsUnset)
						constraintFailed = true;
					argIdx++;
					return;
				}
				if (parameterElement->resolvedTypeConstraint.isResolved()) {
					bool compileTimeKnown = argIdx < argCompileTimeKnown.size() && argCompileTimeKnown[argIdx];
					if (!parameterElement->resolvedTypeConstraint.accepts(argType, compileTimeKnown)) {
						constraintFailed = true;
						argIdx++;
						return;
					}
					score += parameterElement->resolvedTypeConstraint.structuralSpecificity();
				}
				argIdx++;
			}
			);
			if (argIdx != argTypes.size())
				constraintFailed = true;
			if (constraintFailed)
				continue;
			if (score > bestScore) {
				bestScore = score;
				best = {candidate, pathIndex};
			}
		}
	}

	return best;
}
