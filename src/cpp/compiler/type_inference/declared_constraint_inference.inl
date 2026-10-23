static bool typeContainsSymbolicClassInstantiation(const DataType &type) {
	return (type.classDefinition && type.classInstIndex < -1) ||
		   (type.arrayElementType && typeContainsSymbolicClassInstantiation(*type.arrayElementType));
}

static PatternTypeConstraintProbe probeDeclaredTypeConstraint(
	DeclaredTypeConstraintWorkItem &item, ParseContext &parseContext, ResolvedPatternConstraint *resolvedResult = nullptr
) {
	item.failureDiagnostic.reset();
	GroupingSnapshot originalGrouping = captureGroupingSnapshot(item.expression);
	resetExpressionTypes(item.expression);
	InferenceContext::TrialJournal journal;
	InferenceContext trialContext(parseContext, true);
	Instantiation signatureInstantiation;
	trialContext.currentInstantiation = &signatureInstantiation;
	trialContext.trialJournal = &journal;
	trialContext.unresolvedPatternConstraintSignal = std::make_shared<bool>(false);
	trialContext.detectGroupingAmbiguity = false;
	Expression *trialExpression = item.expression;
	bool inferred = inferExpression(trialExpression, trialContext, false, {}, false) && trialContext.typesValid;
	bool deferred = *trialContext.unresolvedPatternConstraintSignal;
	TypeConstraint constraint;
	DataType parameterType;
	bool producedType = inferred && readPatternTypeConstraintValue(trialExpression, trialContext, constraint, parameterType);
	bool pure = signatureInstantiation.purity == InstantiationPurity::Pure;
	if (resolvedResult && inferred && producedType && pure && !deferred) {
		*resolvedResult = {
			constraint, constraint.requiresCompileTimeValue, false, constraint.explicitlyAcceptsNothing(),
			!typeContainsSymbolicClassInstantiation(parameterType)
		};
	}
	if (!deferred && (!inferred || !producedType) && trialContext.hasTypeFailureDiagnostic)
		item.failureDiagnostic = trialContext.typeFailureDiagnostic;
	rollbackTrialJournal(journal);
	applyGroupingSnapshot(originalGrouping);
	item.expression = originalGrouping.root;
	recomputeRanges(item.expression);
	resetExpressionTypes(item.expression);
	if (deferred)
		return PatternTypeConstraintProbe::Deferred;
	if (!inferred || !producedType)
		return PatternTypeConstraintProbe::Invalid;
	return pure ? PatternTypeConstraintProbe::Ready : PatternTypeConstraintProbe::Impure;
}

static bool expressionEvaluatesIntrinsic(Expression *expression, IntrinsicKind kind) {
	while (expression && expression->kind == Expression::Kind::PatternCall && expression->inferredFlexExpansion)
		expression = expression->inferredFlexExpansion;
	return expression && expression->kind == Expression::Kind::IntrinsicCall &&
		   intrinsicKind(expression->intrinsicName) == kind;
}

// Records the properties of a `numbers are ...` rule once its line is inferred.
static bool commitNumberRule(DeclaredTypeConstraintWorkItem &item, ParseContext &parseContext, const TypeConstraint &rule) {
	CodeLine *line = item.numberRuleLine;
	line->expression = item.expression;
	auto *rulesSection = static_cast<ScopeRulesSection *>(line->section);
	if (!expressionEvaluatesIntrinsic(item.expression, IntrinsicKind::NumberRule)) {
		parseContext.diagnostics.push_back(
			Diagnostic(parseContext, Diagnostic::Level::Error, "number rule not a rule", item.diagnosticRange)
		);
		return false;
	}
	TypeConstraint number = TypeConstraint::any();
	number.requiresNumeric = true;
	if (rule.equivalentTo(number)) {
		parseContext.diagnostics.push_back(Diagnostic(
			parseContext, Diagnostic::Level::Warning, "number rule adds nothing", item.diagnosticRange, "rule",
			item.diagnosticRange.subString
		));
	}
	rulesSection->rules.numbers = NumberScopeRule{{item.diagnosticRange, rulesSection->absolute}, rule};
	rulesSection->rules.numberRulePending = false;
	return true;
}

static void commitDeclaredTypeConstraint(DeclaredTypeConstraintWorkItem &item, ParseContext &parseContext) {
	InferenceContext context(parseContext);
	Instantiation signatureInstantiation;
	context.currentInstantiation = &signatureInstantiation;
	context.unresolvedPatternConstraintSignal = std::make_shared<bool>(false);
	resetExpressionTypes(item.expression);
	Expression *expression = item.expression;
	bool inferred = inferExpression(expression, context, false, {}, false) && context.typesValid;
	requireCompilerInvariant(
		!*context.unresolvedPatternConstraintSignal,
		"committed pattern type-constraint inference encountered an unresolved signature dependency"
	);
	requireCompilerInvariant(inferred, "pattern type-constraint probe and committed inference disagreed");
	requireCompilerInvariant(
		signatureInstantiation.purity == InstantiationPurity::Pure,
		"pattern type-constraint probe and committed purity classification disagreed"
	);
	TypeConstraint constraint;
	DataType parameterType;
	requireCompilerInvariant(
		readPatternTypeConstraintValue(expression, context, constraint, parameterType),
		"pattern type-constraint probe and committed result type disagreed"
	);
	item.expression = expression;
	if (item.numberRuleLine) {
		item.numberRuleConstraint = std::move(constraint);
	} else if (item.element) {
		item.element->resolvedTypeConstraint = std::move(constraint);
		item.element->resolvedParameterType = std::move(parameterType);
	} else {
		requireCompilerInvariant(
			item.variable && item.variableReference, "declared variable type-constraint work item has no target"
		);
		item.variableReference->declaredTypeConstraint = std::move(constraint);
		item.variableReference->declaredType = std::move(parameterType);
	}
}

static bool classifyPatternTypeConstraintDependencies(
	ParseContext &parseContext, PatternDefinition &definition, DefinitionPatternElement &element, Expression *expression
) {
	(void)parseContext;
	bool referencesPatternParameter = visitExpressionTree(expression, [&](Expression *current) {
		if (current->kind != Expression::Kind::Variable || !current->variable)
			return false;
		VariableReference *referencedDefinition = normalizeBindingReference(current->variable);
		auto sectionDefinition = definition.section->variableDefinitions.find(referencedDefinition->name);
		return sectionDefinition != definition.section->variableDefinitions.end() &&
			   normalizeBindingReference(sectionDefinition->second) == referencedDefinition;
	});
	element.hasDependentTypeConstraint = referencesPatternParameter;
	return true;
}

static bool collectDeclaredTypeConstraintWorkItems(
	ParseContext &parseContext, Section *section, std::vector<DeclaredTypeConstraintWorkItem> &items
) {
	if (auto *rulesSection = dynamic_cast<ScopeRulesSection *>(section)) {
		for (CodeLine *line : section->codeLines) {
			if (!line->expression)
				continue;
			Range ruleRange(line, line->patternText);
			ScopeRules &rules = rulesSection->rules;
			if (rules.numberRulePending) {
				const SyntaxConfig &syntax = syntaxConfigForRange(parseContext, ruleRange);
				Diagnostic diagnostic(
					parseContext, Diagnostic::Level::Error, "scope rule repeated", ruleRange, "rule", line->patternText
				);
				auto first = std::find_if(items.begin(), items.end(), [&](const DeclaredTypeConstraintWorkItem &item) {
					return item.numberRuleLine &&
						   &static_cast<ScopeRulesSection *>(item.numberRuleLine->section)->rules == &rules;
				});
				requireCompilerInvariant(first != items.end(), "pending number rule has no work item");
				diagnostic.relatedInfo.push_back(
					{renderConfiguredMessage(syntax, "scope rule repeated", "related rule"), first->diagnosticRange}
				);
				parseContext.diagnostics.push_back(std::move(diagnostic));
				return false;
			}
			rules.numberRulePending = true;
			DeclaredTypeConstraintWorkItem item;
			item.diagnosticRange = ruleRange;
			item.typeConstraintName = std::string(line->patternText);
			item.expression = line->expression;
			item.numberRuleLine = line;
			items.push_back(std::move(item));
		}
		return true;
	}
	for (PatternDefinition *definition : section->patternDefinitions) {
		bool definitionValid = true;
		std::function<void(std::vector<DefinitionPatternElement> &)> collectElements =
			[&](std::vector<DefinitionPatternElement> &elements) {
			for (DefinitionPatternElement &element : elements) {
				if (!definitionValid)
					return;
				if (element.type == PatternElement::Type::Choice) {
					for (auto &alternative : element.alternatives)
						collectElements(alternative);
					continue;
				}
				if (element.typeConstraintName.empty() || element.resolvedTypeConstraint.isResolved())
					continue;
				Range constraintRange = patternElementTypeConstraintRange(*definition, element);
				Expression *expression = createTypeConstraintExpression(parseContext, definition->section, constraintRange);
				if (expression && !classifyPatternTypeConstraintDependencies(parseContext, *definition, element, expression)) {
					destroyTypeConstraintExpression(expression);
					definitionValid = false;
					return;
				}
				if (element.hasDependentTypeConstraint) {
					element.resolvedTypeConstraint = TypeConstraint::any();
					destroyTypeConstraintExpression(expression);
					continue;
				}
				items.push_back(
					{definition, &element, nullptr, nullptr, definition->range, element.typeConstraintName, expression,
					 std::nullopt, nullptr, TypeConstraint{}}
				);
			}
		};
		collectElements(definition->patternElements);
		if (!definitionValid)
			return false;
	}
	for (auto &[name, variable] : section->variables) {
		(void)name;
		if (!variable || !variable->hasDeclaredTypeConstraint())
			continue;
		for (VariableReference *reference : variable->declaredTypeConstraintReferences) {
			requireCompilerInvariant(
				reference && !reference->declaredTypeConstraintName.empty(),
				"variable constraint declaration has no source constraint"
			);
			Expression *expression =
				createTypeConstraintExpression(parseContext, section, reference->declaredTypeConstraintRange);
			Section *definitionSection = enclosingPatternDefinitionSection(section);
			if (expression && definitionSection && expressionReferencesSectionPatternParameter(definitionSection, expression)) {
				reference->hasDependentTypeConstraint = true;
				destroyTypeConstraintExpression(expression);
				continue;
			}
			items.push_back(
				{nullptr, nullptr, variable, reference, reference->declaredTypeConstraintRange,
				 reference->declaredTypeConstraintName, expression, std::nullopt, nullptr, TypeConstraint{}}
			);
		}
	}
	for (Section *child : section->children) {
		if (!collectDeclaredTypeConstraintWorkItems(parseContext, child, items))
			return false;
	}
	return true;
}

#include "dependent_constraint_compilation.inl"
#include "variable_type_constraint_materialization.inl"

static bool inferDeclaredTypeConstraints(ParseContext &parseContext) {
	struct ProvisionalDomainScope {
		ParseContext &context;
		~ProvisionalDomainScope() {
			requireCompilerInvariant(context.activePatternConstraintProbes.empty(), "signature probe scope did not unwind");
			context.provisionalConstraintDomains.clear();
		}
	} provisionalDomainScope{parseContext};
	materializeExplicitPatternParameterDefinitions(parseContext);
	std::vector<DeclaredTypeConstraintWorkItem> items;
	size_t diagnosticsBeforeParsing = parseContext.diagnostics.size();
	if (!collectDeclaredTypeConstraintWorkItems(parseContext, parseContext.mainSection, items))
		return false;
	auto destroyExpressions = [&]() {
		for (DeclaredTypeConstraintWorkItem &item : items) {
			if (!item.expression)
				continue;
			if (!item.numberRuleLine)
				destroyTypeConstraintExpression(item.expression);
			item.expression = nullptr;
		}
	};
	for (const DeclaredTypeConstraintWorkItem &item : items) {
		if (item.expression)
			continue;
		if (parseContext.diagnostics.size() == diagnosticsBeforeParsing) {
			parseContext.diagnostics.push_back(
				unknownTypeConstraintDiagnostic(parseContext, item.diagnosticRange, item.typeConstraintName)
			);
		}
		destroyExpressions();
		return false;
	}

	size_t unresolvedCount = items.size();
	while (unresolvedCount > 0) {
		if (!initializePatternPathSignatures(parseContext)) {
			destroyExpressions();
			return false;
		}
		bool madeProgress = false;
		for (DeclaredTypeConstraintWorkItem &item : items) {
			if (!item.expression)
				continue;
			if ((!item.definition || !item.element) && (!item.variable || !item.variableReference) && !item.numberRuleLine) {
				destroyExpressions();
				crashCompilerBug("declared type-constraint work item lost its target");
			}
			if (!item.numberRuleLine) {
				// Code waits for the number rules of its scope.
				const CodeLine *governedLine = item.element ? item.definition->range.line : item.variableReference->range.line;
				bool numberRulePending = false;
				effectiveNumberRule(parseContext, governedLine, &numberRulePending);
				if (numberRulePending)
					continue;
			}
			PatternTypeConstraintProbe probe = probeDeclaredTypeConstraint(item, parseContext);
			if (probe == PatternTypeConstraintProbe::Deferred)
				continue;
			if (probe == PatternTypeConstraintProbe::Invalid || probe == PatternTypeConstraintProbe::Impure) {
				if (probe == PatternTypeConstraintProbe::Invalid && item.failureDiagnostic) {
					parseContext.diagnostics.push_back(std::move(*item.failureDiagnostic));
				} else if (probe == PatternTypeConstraintProbe::Invalid) {
					parseContext.diagnostics.push_back(
						unknownTypeConstraintDiagnostic(parseContext, item.diagnosticRange, item.typeConstraintName)
					);
				} else {
					parseContext.diagnostics.push_back(Diagnostic(
						parseContext, Diagnostic::Level::Error, "impure type constraint", item.diagnosticRange,
						"type_constraint", item.typeConstraintName
					));
				}
				destroyExpressions();
				return false;
			}
			commitDeclaredTypeConstraint(item, parseContext);
			if (item.numberRuleLine) {
				if (!commitNumberRule(item, parseContext, item.numberRuleConstraint)) {
					item.expression = nullptr;
					destroyExpressions();
					return false;
				}
			} else {
				destroyTypeConstraintExpression(item.expression);
			}
			item.expression = nullptr;
			unresolvedCount--;
			madeProgress = true;
		}
		if (madeProgress)
			continue;
		auto unresolved = std::find_if(items.begin(), items.end(), [](const DeclaredTypeConstraintWorkItem &item) {
			return item.expression != nullptr;
		});
		requireCompilerInvariant(unresolved != items.end(), "type-constraint worklist lost its unresolved item");
		parseContext.diagnostics.push_back(Diagnostic(
			parseContext, Diagnostic::Level::Error, "cyclic type constraint", unresolved->diagnosticRange, "type_constraint",
			unresolved->typeConstraintName
		));
		destroyExpressions();
		return false;
	}
	return finalizeVariableTypeConstraints(parseContext) && compileDependentPatternSignatures(parseContext) &&
		   compileDependentLocalVariableConstraints(parseContext);
}
