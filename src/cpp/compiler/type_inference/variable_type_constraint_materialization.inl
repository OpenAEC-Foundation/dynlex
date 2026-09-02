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
			VariableReference *first = references.front();
			requireCompilerInvariant(first->declaredTypeConstraint.isResolved(), "variable type constraint was not resolved");
			variable->declaredTypeConstraint = first->declaredTypeConstraint;
			variable->declaredType = first->declaredType;
			for (size_t index = 1; index < references.size(); index++) {
				VariableReference *reference = references[index];
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

static bool validatePatternParameterVariableConstraints(ParseContext &parseContext) {
	bool valid = true;
	std::function<void(Section *)> visit = [&](Section *section) {
		for (PatternDefinition *definition : section->patternDefinitions) {
			forEachLeafElement(definition->patternElements, [&](DefinitionPatternElement &element) {
				if (!valid || element.type != PatternElement::Type::Variable || element.typeConstraintName.empty() ||
					element.hasDependentTypeConstraint)
					return;
				auto variable = section->variables.find(element.text);
				if (variable == section->variables.end() || !variable->second ||
					!variable->second->declaredTypeConstraint.isResolved())
					return;
				Variable *parameter = variable->second;
				if (element.resolvedTypeConstraint.equivalentTo(parameter->declaredTypeConstraint))
					return;
				VariableReference *second = parameter->firstDeclaredTypeConstraintReference();
				requireCompilerInvariant(second, "constrained pattern parameter has no variable constraint source");
				Range firstRange = patternElementTypeConstraintRange(*definition, element);
				Diagnostic diagnostic(
					parseContext, Diagnostic::Level::Error, "conflicting variable type constraints",
					second->declaredTypeConstraintRange, "name", parameter->name, "first_type", element.typeConstraintName,
					"second_type", second->declaredTypeConstraintName
				);
				diagnostic.relatedInfo.push_back({"Variable '" + parameter->name + "' was first constrained here", firstRange});
				parseContext.addDiagnostic(std::move(diagnostic));
				valid = false;
			});
		}
		for (Section *child : section->children)
			visit(child);
	};
	visit(parseContext.mainSection);
	return valid;
}
