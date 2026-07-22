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

const DefinitionPatternElement *matchedPatternParameterElement(PatternDefinition *definition, PatternTreeNode *matchedNode) {
	if (!definition || !matchedNode)
		return nullptr;
	auto nameIt = matchedNode->parameterNames.find(definition);
	auto startIt = matchedNode->definitionStartPositions.find(definition);
	if (nameIt == matchedNode->parameterNames.end() || startIt == matchedNode->definitionStartPositions.end())
		return nullptr;
	return findDefinitionParameterElementAt(definition->patternElements, nameIt->second, startIt->second);
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
	PatternDefinition *definition, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::vector<PatternTreeNode *> &nodesPassed, const std::vector<DataType> &argTypes
) {
	requireCompilerInvariant(paramBindings.size() == argTypes.size(), "pattern parameter bindings and types diverged");
	std::unordered_set<std::string> requiredParameters;
	size_t bindingIndex = 0;
	forEachPatternParameterName(nodesPassed, definition, [&](const std::string &parameterName, PatternTreeNode *node) {
		requireCompilerInvariant(bindingIndex < argTypes.size(), "matched pattern has more parameters than call arguments");
		const DefinitionPatternElement *parameterElement = matchedPatternParameterElement(definition, node);
		requireCompilerInvariant(parameterElement != nullptr, "matched pattern parameter has no definition element");
		if (patternParameterRequiresCompileTimeValue(*parameterElement, argTypes[bindingIndex]))
			requiredParameters.insert(parameterName);
		bindingIndex++;
	});
	requireCompilerInvariant(bindingIndex == argTypes.size(), "matched pattern has fewer parameters than call arguments");
	return requiredParameters;
}

PatternDefinition *selectOverload(
	const std::vector<PatternDefinition *> &definitions, const std::vector<Expression *> & /*sortedArgs*/,
	const std::vector<PatternTreeNode *> &nodesPassed, const std::vector<DataType> &argTypes,
	const std::vector<bool> &argCompileTimeKnown
) {
	if (definitions.empty())
		return nullptr;

	// Score each candidate: count how many type constraints match
	PatternDefinition *best = nullptr;
	int bestScore = -1;

	for (auto *candidate : definitions) {
		int score = 0;
		bool constraintFailed = false;

		size_t argIdx = 0;
		forEachPatternParameterName(nodesPassed, candidate, [&](const std::string &, PatternTreeNode *node) {
			if (constraintFailed || argIdx >= argTypes.size()) {
				argIdx++;
				return;
			}
			const DefinitionPatternElement *parameterElement = matchedPatternParameterElement(candidate, node);
			requireCompilerInvariant(parameterElement != nullptr, "overload parameter has no definition element");
			const DataType &argType = argTypes[argIdx];
			if (!argType.isDeduced()) {
				bool acceptsUnset =
					candidate->section && candidate->section->isFlex && !parameterElement->resolvedTypeConstraint.isResolved();
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
		});

		if (constraintFailed)
			continue;

		if (score > bestScore) {
			bestScore = score;
			best = candidate;
		}
	}

	return best;
}
