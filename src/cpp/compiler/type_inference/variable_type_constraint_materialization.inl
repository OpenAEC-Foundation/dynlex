static void materializeExplicitPatternParameterDefinitions(ParseContext &parseContext) {
	std::function<void(Section *)> visit = [&](Section *section) {
		std::unordered_set<std::string> materializedNames;
		for (PatternDefinition *definition : section->patternDefinitions) {
			for (const auto &path : definition->indexedPaths) {
				for (const PatternElement &element : path) {
					if (element.type != PatternElement::Type::Variable || !materializedNames.insert(element.text).second)
						continue;
					int sourceStart = definition->range.start() + static_cast<int>(element.startPos);
					Range sourceRange(definition->range.line, sourceStart, sourceStart + static_cast<int>(element.text.size()));
					requireCompilerInvariant(
						section->resolvePatternParameterBinding(parseContext, element.text, sourceRange),
						"indexed explicit pattern parameter has no binding definition"
					);
				}
			}
		}
		for (Section *child : section->children)
			visit(child);
	};
	visit(parseContext.mainSection);
}

static bool finalizeVariableTypeConstraints(ParseContext &parseContext) {
	bool valid = true;
	std::function<void(Section *)> visit = [&](Section *section) {
		for (auto &[name, variable] : section->variables) {
			(void)name;
			if (!variable || !variable->hasDeclaredTypeConstraint())
				continue;
			auto &references = variable->declaredTypeConstraintReferences;
			std::stable_sort(references.begin(), references.end(), [](VariableReference *left, VariableReference *right) {
				requireCompilerInvariant(
					left && left->range.line && right && right->range.line,
					"variable type-constraint declaration has no source location"
				);
				return std::pair(left->range.line->mergedLineIndex, left->range.start()) <
					   std::pair(right->range.line->mergedLineIndex, right->range.start());
			});
			auto firstFixed = std::find_if(references.begin(), references.end(), [](VariableReference *reference) {
				return !reference->hasDependentTypeConstraint;
			});
			if (firstFixed == references.end())
				continue;
			VariableReference *first = *firstFixed;
			requireCompilerInvariant(first->declaredTypeConstraint.isResolved(), "variable type constraint was not resolved");
			variable->declaredTypeConstraint = first->declaredTypeConstraint;
			variable->declaredType = first->declaredType;
			for (size_t index = 1; index < references.size(); index++) {
				VariableReference *reference = references[index];
				if (reference->hasDependentTypeConstraint || reference == first)
					continue;
				requireCompilerInvariant(
					reference->declaredTypeConstraint.isResolved(), "variable type constraint was not resolved"
				);
				if (first->declaredTypeConstraint.equivalentTo(reference->declaredTypeConstraint))
					continue;
				Diagnostic diagnostic(
					parseContext, Diagnostic::Level::Error, "conflicting variable type constraints",
					reference->declaredTypeConstraintRange, "name", variable->name, "first_type",
					first->declaredTypeConstraintName, "second_type", reference->declaredTypeConstraintName
				);
				diagnostic.relatedInfo.push_back(
					{"Variable '" + variable->name + "' was first constrained here", first->declaredTypeConstraintRange}
				);
				parseContext.addDiagnostic(std::move(diagnostic));
				valid = false;
				break;
			}
		}
		for (Section *child : section->children)
			visit(child);
	};
	visit(parseContext.mainSection);
	return valid;
}
