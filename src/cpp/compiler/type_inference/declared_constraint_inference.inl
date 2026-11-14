static bool typeContainsSymbolicClassInstantiation(const DataType &type) {
	return (type.classDefinition && type.classInstIndex < -1) ||
		   (type.arrayElementType && typeContainsSymbolicClassInstantiation(*type.arrayElementType));
}

static PatternTypeConstraintProbe probeDeclaredTypeConstraint(
	DeclaredTypeConstraintWorkItem &item, ParseContext &parseContext, ResolvedPatternConstraint *resolvedResult,
	InferenceContext::DependencyReads reads
) {
	item.failureDiagnostic.reset();
	GroupingSnapshot originalGrouping = captureGroupingSnapshot(item.expression);
	resetExpressionTypes(item.expression);
	InferenceContext::TrialJournal journal;
	InferenceContext trialContext(parseContext, true);
	trialContext.dependencyReads = reads;
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
	rollbackTrialJournal(trialContext, journal);
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
		parseContext.addDiagnostic(
			Diagnostic(parseContext, Diagnostic::Level::Error, "number rule not a rule", item.diagnosticRange)
		);
		return false;
	}
	TypeConstraint number = TypeConstraint::any();
	number.requiresNumeric = true;
	if (rule.equivalentTo(number)) {
		parseContext.addDiagnostic(Diagnostic(
			parseContext, Diagnostic::Level::Warning, "number rule adds nothing", item.diagnosticRange, "rule",
			item.diagnosticRange.subString
		));
	}
	rulesSection->rules.numbers = NumberScopeRule{{item.diagnosticRange, rulesSection->absolute}, rule};
	rulesSection->rules.numberRulePending = false;
	return true;
}

static void commitDeclaredTypeConstraint(DeclaredTypeConstraintWorkItem &item, ParseContext &parseContext, size_t index) {
	InferenceContext context(parseContext);
	context.dependencyReads.constraintProbe = index;
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

#include "declared_constraint_identity.inl"

static std::unordered_map<const VariableReference *, const Section *> variableDefinitionOwners(const Section *root) {
	std::unordered_map<const VariableReference *, const Section *> owners;
	const auto visit = [&](auto &&self, const Section *section) -> void {
		for (const auto &[name, definition] : section->variableDefinitions) {
			(void)name;
			requireCompilerInvariant(definition, "section variable definition is null");
			auto [found, inserted] = owners.emplace(normalizeBindingReference(definition), section);
			requireCompilerInvariant(inserted || found->second == section, "variable definition belongs to multiple sections");
		}
		for (const Section *child : section->children)
			self(self, child);
	};
	visit(visit, root);
	return owners;
}

static void recordDeclaredConstraintDependencies(
	const DeclaredTypeConstraintWorkItem &item, ParseContext &context,
	const std::unordered_map<const VariableReference *, const Section *> &variableOwners,
	std::unordered_set<const Section *> &dependencies
) {
	auto addVariableOwner = [&](VariableReference *reference) {
		requireCompilerInvariant(reference, "type-constraint expression has a null variable reference");
		const VariableReference *definition = normalizeBindingReference(reference);
		if (auto lookup = context.unboundConstraintVariableScopes.find(definition);
			lookup != context.unboundConstraintVariableScopes.end()) {
			// This read searched the lexical scopes without finding a binding.
			// A later declaration can change that result, even if this candidate
			// loses overload selection and never emits a diagnostic.
			for (Section *scope = lookup->second; scope; scope = scope->parent)
				if (scope == context.mainSection || !scope->patternDefinitions.empty())
					dependencies.insert(scope);
			return;
		}
		auto owner = variableOwners.find(definition);
		requireCompilerInvariant(owner != variableOwners.end(), "type-constraint variable has no owning section");
		for (const Section *section = owner->second; section; section = section->parent)
			if (section == context.mainSection ||
				(section->type == SectionType::Function && !section->patternDefinitions.empty())) {
				dependencies.insert(section);
				break;
			}
	};
	if (item.variableReference)
		addVariableOwner(item.variableReference);
	auto visitMatch = [&](auto &&self, const PatternMatch &match) -> void {
		for (const PatternDefinition *definition : match.matchingDefinitions)
			dependencies.insert(definition->section);
		for (const PatternMatch &child : match.subMatches)
			self(self, child);
	};
	visitExpressionTree(item.expression, [&](Expression *expression) {
		for (const PatternDefinition *definition :
			 {expression->selectedPatternDefinition, expression->selectedCallableDefinition})
			if (definition)
				dependencies.insert(definition->section);
		if (expression->patternMatch)
			visitMatch(visitMatch, *expression->patternMatch);
		if (expression->kind == Expression::Kind::Variable)
			addVariableOwner(expression->variable);
		return false;
	});
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

#include "declared_constraint_reuse.inl"

static bool collectDeclaredTypeConstraintWorkItems(
	ParseContext &parseContext, Section *section, std::vector<DeclaredTypeConstraintWorkItem> &items,
	DeclaredConstraintIdentities &identities
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
			item.sourceSection = section;
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
				DeclaredTypeConstraintWorkItem item;
				item.definition = definition;
				item.element = &element;
				item.diagnosticRange = definition->range;
				item.typeConstraintName = element.typeConstraintName;
				bool omitExpression = canOmitReusableConstraintExpression(parseContext, item, identities);
				Expression *expression =
					omitExpression ? nullptr
								   : createTypeConstraintExpression(parseContext, definition->section, constraintRange);
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
				item.expression = expression;
				item.expressionOmittedForReuse = omitExpression;
				item.sourceSection = definition->section;
				items.push_back(std::move(item));
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
			DeclaredTypeConstraintWorkItem item;
			item.variable = variable;
			item.variableReference = reference;
			item.diagnosticRange = reference->declaredTypeConstraintRange;
			item.typeConstraintName = reference->declaredTypeConstraintName;
			item.sourceSection = section;
			bool omitExpression = canOmitReusableConstraintExpression(parseContext, item, identities);
			Expression *expression =
				omitExpression ? nullptr
							   : createTypeConstraintExpression(parseContext, section, reference->declaredTypeConstraintRange);
			Section *definitionSection = enclosingPatternDefinitionSection(section);
			if (expression && definitionSection && expressionReferencesSectionPatternParameter(definitionSection, expression)) {
				reference->hasDependentTypeConstraint = true;
				destroyTypeConstraintExpression(expression);
				continue;
			}
			item.expression = expression;
			item.expressionOmittedForReuse = omitExpression;
			item.sourceSection = section;
			items.push_back(std::move(item));
		}
	}
	for (Section *child : section->children) {
		if (!collectDeclaredTypeConstraintWorkItems(parseContext, child, items, identities))
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
	DeclaredConstraintIdentities identities(parseContext);
	size_t diagnosticsBeforeParsing = parseContext.diagnostics.size();
	if (!collectDeclaredTypeConstraintWorkItems(parseContext, parseContext.mainSection, items, identities))
		return false;
	const auto variableOwners = variableDefinitionOwners(parseContext.mainSection);
	using Query = ParseContext::IncrementalConstraintQuery;
	parseContext.constraintQueries.clear();
	for (const DeclaredTypeConstraintWorkItem &item : items) {
		Query query;
		query.identity = identities.identity(item);
		query.text = item.typeConstraintName;
		parseContext.constraintQueries.push_back(std::move(query));
	}
	alignDeclaredConstraintQueries(parseContext, identities);
	parseContext.collectedConstraintQueries = true;
	for (DeclaredTypeConstraintWorkItem &item : items) {
		if (!item.expressionOmittedForReuse)
			continue;
		parseContext.constraintExpressionsReused++;
		if (parseContext.retainParsedTemporaryExpression) {
			Range sourceRange = item.element ? patternElementTypeConstraintRange(*item.definition, *item.element)
											 : item.variableReference->declaredTypeConstraintRange;
			parseContext.retainParsedTemporaryExpression(sourceRange, SectionType::Function);
		}
	}
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
		if (item.expression || item.expressionOmittedForReuse)
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
	struct DiagnosticProductScope {
		ParseContext &context;
		std::vector<Diagnostic> *previous;
		DiagnosticProductScope(ParseContext &context, std::vector<Diagnostic> &product)
			: context(context), previous(std::exchange(context.activeDiagnosticProduct, &product)) {}
		~DiagnosticProductScope() { context.activeDiagnosticProduct = previous; }
	};
	int pass = 0;
	while (unresolvedCount > 0) {
		pass++;
		if (!initializePatternPathSignatures(parseContext)) {
			destroyExpressions();
			return false;
		}
		bool madeProgress = false;
		for (size_t itemIndex = 0; itemIndex < items.size(); itemIndex++) {
			DeclaredTypeConstraintWorkItem &item = items[itemIndex];
			if (!item.expression && !item.expressionOmittedForReuse)
				continue;
			Query &query = parseContext.constraintQueries[itemIndex];
			const Query *prior =
				parseContext.priorConstraintQueries.empty() ? nullptr : &parseContext.priorConstraintQueries.at(itemIndex);
			bool reuseItem = prior && prior->reusable;
			if (reuseItem && prior->state == Query::State::Blocked) {
				// A fixed-point product proves deferral, not a per-query error.
				// Independent queries must still run before a cycle is diagnosed.
				DiagnosticProductScope diagnosticScope(parseContext, query.diagnostics);
				query.state = prior->state;
				query.pass = pass;
				query.dependencies = prior->dependencies;
				query.readsReused = true;
				for (const Diagnostic &diagnostic : prior->diagnostics)
					parseContext.addDiagnostic(diagnostic);
				parseContext.constraintProbesReused++;
				continue;
			}
			if (reuseItem) {
				requireCompilerInvariant(
					prior->pass >= pass && prior->state != Query::State::Pending,
					"reused type-constraint probe passed before its pending item"
				);
				if (prior->pass > pass)
					continue;
			}
			if ((!item.definition || !item.element) && (!item.variable || !item.variableReference) && !item.numberRuleLine) {
				destroyExpressions();
				crashCompilerBug("declared type-constraint work item lost its target");
			}
			if (!item.numberRuleLine) {
				// Code waits for the number rules of its scope.
				const CodeLine *governedLine = item.element ? item.definition->range.line : item.variableReference->range.line;
				bool numberRulePending = false;
				effectiveNumberRule(parseContext, governedLine, &numberRulePending);
				if (numberRulePending) {
					requireCompilerInvariant(!reuseItem, "reused type-constraint pass still waits for a number rule");
					continue;
				}
			}
			DiagnosticProductScope diagnosticScope(parseContext, query.diagnostics);
			if (reuseItem && prior->state == Query::State::Failed) {
				query.state = prior->state;
				query.pass = pass;
				query.dependencies = prior->dependencies;
				query.readsReused = true;
				for (const Diagnostic &diagnostic : prior->diagnostics)
					parseContext.addDiagnostic(diagnostic);
				parseContext.constraintProbesReused++;
				destroyExpressions();
				return false;
			}
			PatternTypeConstraintProbe probe = PatternTypeConstraintProbe::Ready;
			if (reuseItem)
				parseContext.constraintProbesReused++;
			else {
				// Keep every syntactic candidate and binding read, including
				// attempts that defer or later lose a grouping trial.
				recordDeclaredConstraintDependencies(item, parseContext, variableOwners, query.dependencies);
				probe = probeDeclaredTypeConstraint(item, parseContext, nullptr, {.constraintProbe = itemIndex});
				parseContext.constraintProbesRun++;
			}
			if (probe == PatternTypeConstraintProbe::Deferred)
				continue;
			if (probe == PatternTypeConstraintProbe::Invalid || probe == PatternTypeConstraintProbe::Impure) {
				if (probe == PatternTypeConstraintProbe::Invalid && item.failureDiagnostic) {
					parseContext.addDiagnostic(std::move(*item.failureDiagnostic));
				} else if (probe == PatternTypeConstraintProbe::Invalid) {
					parseContext.addDiagnostic(
						unknownTypeConstraintDiagnostic(parseContext, item.diagnosticRange, item.typeConstraintName)
					);
				} else {
					parseContext.addDiagnostic(Diagnostic(
						parseContext, Diagnostic::Level::Error, "impure type constraint", item.diagnosticRange,
						"type_constraint", item.typeConstraintName
					));
				}
				query.state = Query::State::Failed;
				query.pass = pass;
				destroyExpressions();
				return false;
			}
			bool reuseValue = reuseItem && !item.numberRuleLine && prior->value;
			if (reuseValue) {
				const auto &value = *prior->value;
				if (item.element) {
					item.element->resolvedTypeConstraint = value.constraint;
					item.element->resolvedParameterType = value.parameterType;
				} else {
					requireCompilerInvariant(item.variableReference, "reused declared type constraint has no target");
					item.variableReference->declaredTypeConstraint = value.constraint;
					item.variableReference->declaredType = value.parameterType;
				}
				query.value = value;
				query.dependencies = prior->dependencies;
				for (const Diagnostic &diagnostic : prior->diagnostics)
					parseContext.addDiagnostic(diagnostic);
				parseContext.constraintValuesReused++;
			} else {
				commitDeclaredTypeConstraint(item, parseContext, itemIndex);
				if (item.element)
					query.value = ParseContext::IncrementalConstraintValue{
						item.element->resolvedTypeConstraint, item.element->resolvedParameterType
					};
				else if (item.variableReference)
					query.value = ParseContext::IncrementalConstraintValue{
						item.variableReference->declaredTypeConstraint, item.variableReference->declaredType
					};
				recordDeclaredConstraintDependencies(item, parseContext, variableOwners, query.dependencies);
			}
			query.state = Query::State::Ready;
			query.pass = pass;
			query.readsReused = reuseItem;
			if (item.numberRuleLine) {
				if (!commitNumberRule(item, parseContext, item.numberRuleConstraint)) {
					query.state = Query::State::Failed;
					item.expression = nullptr;
					destroyExpressions();
					return false;
				}
			} else {
				destroyTypeConstraintExpression(item.expression);
			}
			item.expression = nullptr;
			item.expressionOmittedForReuse = false;
			unresolvedCount--;
			madeProgress = true;
		}
		if (madeProgress)
			continue;
		auto unresolved = std::find_if(items.begin(), items.end(), [](const DeclaredTypeConstraintWorkItem &item) {
			return item.expression != nullptr || item.expressionOmittedForReuse;
		});
		requireCompilerInvariant(unresolved != items.end(), "type-constraint worklist lost its unresolved item");
		parseContext.addDiagnostic(Diagnostic(
			parseContext, Diagnostic::Level::Error, "cyclic type constraint", unresolved->diagnosticRange, "type_constraint",
			unresolved->typeConstraintName
		));
		// Blocked owners form one fixed-point frontier. Store each owner
		// once per query; cache invalidation expands the shared frontier.
		for (size_t index = 0; index < items.size(); index++)
			if (items[index].expression || items[index].expressionOmittedForReuse) {
				Section *owner = items[index].sourceSection;
				while (owner->parent && owner->patternDefinitions.empty())
					owner = owner->parent;
				Query &query = parseContext.constraintQueries[index];
				query.state = Query::State::Blocked;
				query.pass = pass;
				query.dependencies.insert(owner);
			}
		destroyExpressions();
		return false;
	}
	return finalizeVariableTypeConstraints(parseContext) && compileDependentPatternSignatures(parseContext) &&
		   compileDependentLocalVariableConstraints(parseContext);
}
