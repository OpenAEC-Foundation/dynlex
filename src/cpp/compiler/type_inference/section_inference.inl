#pragma once

#include "operand_reordering.inl"

static Variable *findOwnSectionVariable(Section *section, const std::string &name) {
	if (!section)
		return nullptr;
	auto it = section->variables.find(name);
	return it != section->variables.end() ? it->second : nullptr;
}

static void seedNonFlexSectionParameterState(Section *section, InferenceContext &context) {
	if (!section || !context.currentInstantiation || section->isFlex || section->patternDefinitions.empty())
		return;
	for (const auto &[name, parameterType] : context.currentInstantiation->parameterTypesByName) {
		Variable *parameterVariable = findOwnSectionVariable(section, name);
		if (!parameterVariable || parameterVariable->isGlobal)
			continue;
		parameterVariable->type = concretizeClassType(parameterType);
		parameterVariable->typeOriginRange = parameterVariable->definition ? parameterVariable->definition->range : Range();
		parameterVariable->typeOriginFloatLiteralReplacement.clear();
	}
	for (const auto &[name, value] : context.currentInstantiation->constantParameterValues) {
		Variable *parameterVariable = findOwnSectionVariable(section, name);
		if (parameterVariable) {
			context.setKnownConstant(parameterVariable->definition, value);
		}
	}
}

static bool isLoopSectionOpening(Expression *openingExpression) {
	if (!openingExpression)
		return false;
	Expression *header = openingExpression;
	if (header->kind == Expression::Kind::PatternCall)
		header = header->inferredFlexExpansion;
	return header && header->kind == Expression::Kind::IntrinsicCall &&
		   intrinsicKind(header->intrinsicName) == IntrinsicKind::LoopWhile;
}

static std::optional<std::string> finalizedControlHeaderKind(Expression *expression, InferenceContext &context) {
	if (!expression)
		return std::nullopt;
	Expression *header = expression;
	if (header->kind == Expression::Kind::PatternCall)
		header = context.lookupFlexExpansion(header);
	if (!header || header->kind != Expression::Kind::IntrinsicCall)
		return std::nullopt;
	if (header->intrinsicName != "if" && header->intrinsicName != "else if" && header->intrinsicName != "else")
		return std::nullopt;
	return header->intrinsicName;
}

static std::optional<std::string> inferControlHeaderKindForLookahead(
	Expression *expression, InferenceContext &context, bool alreadyOrdered, const BindingFrameStack &bindingFrameStack
) {
	if (!expression)
		return std::nullopt;
	GroupingSnapshot originalGrouping = captureGroupingSnapshot(expression);
	resetExpressionTypes(expression);
	InferenceContext::TrialJournal journal;
	InferenceContext trialContext(context.parseContext, true);
	trialContext.currentInstantiation = context.currentInstantiation;
	trialContext.currentKnownConstants = context.currentKnownConstants;
	trialContext.inheritedTrialExpressionValues =
		context.trial ? &context.trialExpressionValues : context.inheritedTrialExpressionValues;
	trialContext.trialJournal = &journal;
	trialContext.trialInstantiationCache =
		context.trialInstantiationCache ? context.trialInstantiationCache : context.ensureTrialInstantiationCache();
	trialContext.unresolvedPatternConstraintSignal = context.unresolvedPatternConstraintSignal;
	trialContext.allowTrialSummaryReuse = true;
	trialContext.detectGroupingAmbiguity = false;
	Expression *trialExpression = expression;
	std::optional<std::string> kind;
	if (inferExpression(trialExpression, trialContext, alreadyOrdered, bindingFrameStack, false) && trialContext.typesValid)
		kind = finalizedControlHeaderKind(trialExpression, trialContext);
	rollbackTrialJournal(journal);
	applyGroupingSnapshot(originalGrouping);
	recomputeRanges(expression);
	resetExpressionTypes(expression);
	return kind;
}

static bool inferSection(
	Section *section, InstantiatedSectionBody *body, Expression *openingExpression, InferenceContext &context,
	const BindingFrameStack &bindingFrameStack
) {
	requireCompilerInvariant(!body || body->sourceSection == section, "active instantiated body does not match section");
	// The first valid inference determines operand ordering per code line. Later
	// passes reuse the committed grouping stored on the line itself.
	if (context.trial && context.trialJournal)
		context.trialJournal->recordTouchedSection(section);
	if (context.trial && context.trialJournal) {
		for (auto &[name, variable] : section->variables) {
			(void)name;
			if (!variable || variable->isGlobal)
				continue;
			context.trialJournal->recordVariableWrite(variable);
		}
	}
	resetSectionExpressionTypes(section, body);
	resetSectionLocalVariableTypes(section);
	if (section->isFlex || section->patternDefinitions.empty()) {
		for (auto &[name, boundVar] : section->variables) {
			if (!boundVar)
				continue;
			Expression *boundExpr = bindingFrameStack.lookup(boundVar->definition);
			if (!boundExpr)
				continue;
			Expression *boundExprForType = boundExpr;
			DataType boundType = ensureExpressionTypeWithCurrentGrouping(boundExprForType, context, bindingFrameStack);
			if (!boundType.isDeduced())
				continue;
			if (context.trial && context.trialJournal)
				context.trialJournal->recordVariableWrite(boundVar);
			commitVariableTypeFromValue(boundVar, boundExpr, boundType);
			CompileTimeValue boundValue = context.lookupExpressionValue(boundExpr);
			context.setKnownConstant(boundVar->definition, boundValue);
		}
	} else {
		seedNonFlexSectionParameterState(section, context);
	}

	bool loopSection = isLoopSectionOpening(openingExpression);
	std::unordered_map<VariableReference *, CompileTimeValue> constantsAtLoopEntry;
	if (loopSection) {
		constantsAtLoopEntry = context.currentKnownConstants;
		context.pushLoopMutationScope();
	}
	struct LoopMutationScopeGuard {
		InferenceContext &context;
		bool active;
		explicit LoopMutationScopeGuard(InferenceContext &context, bool active) : context(context), active(active) {}
		std::unordered_set<VariableReference *> finish() {
			if (!active)
				return {};
			active = false;
			return context.popLoopMutationScope();
		}
		~LoopMutationScopeGuard() {
			if (active)
				context.popLoopMutationScope();
		}
	} loopMutationScope(context, loopSection);

	auto controlHeaderInfo = [&](CodeLine *line,
								 Expression *lineExpression) -> std::optional<std::pair<std::string, Expression *>> {
		if (!line || !lineExpression)
			return std::nullopt;

		Expression *header = lineExpression;
		if (header->kind == Expression::Kind::PatternCall)
			header = context.lookupFlexExpansion(header);
		if (!header || header->kind != Expression::Kind::IntrinsicCall)
			return std::nullopt;
		if (header->intrinsicName != "if" && header->intrinsicName != "else if" && header->intrinsicName != "else")
			return std::nullopt;
		return std::make_optional(std::make_pair(header->intrinsicName, header));
	};

	auto inferOpenedSection = [&](CodeLine *line) {
		if (!line || !line->sectionOpening || dynamic_cast<DefinitionSection *>(line->sectionOpening))
			return true;
		InstantiatedSectionBody *openedBody = body ? body->bodyForChild(line->sectionOpening) : nullptr;
		Expression *activeOpeningExpression =
			body && line->sectionOpening ? body->findCloneOf(line->expression) : line->expression;
		if (!inferSection(line->sectionOpening, openedBody, activeOpeningExpression, context, bindingFrameStack)) {
			context.typesValid = false;
			return false;
		}
		return true;
	};
	auto inferLineExpression = [&](CodeLine *line, Expression *&lineExpression) {
		if (!lineExpression)
			return true;
		applyCodeLineGrouping(line, lineExpression, context);
		bool alreadyOrdered = codeLineCanReuseGrouping(line, context);
		bool detectLineGroupingAmbiguity = !context.trial && context.detectGroupingAmbiguity;
		bool previousGroupingAmbiguityIncomplete = context.groupingAmbiguityIncomplete;
		context.groupingAmbiguityIncomplete = false;
		bool inferred = inferExpression(
			lineExpression, context, alreadyOrdered, bindingFrameStack, section->type != SectionType::Replacement
		);
		bool lineGroupingAmbiguityIncomplete = context.groupingAmbiguityIncomplete;
		context.groupingAmbiguityIncomplete = previousGroupingAmbiguityIncomplete || lineGroupingAmbiguityIncomplete;
		if (!inferred) {
			context.typesValid = false;
			return false;
		}
		Section *inferenceRootSection = context.currentInstantiation && context.currentInstantiation->body
											? context.currentInstantiation->body->sourceSection
											: context.parseContext.mainSection;
		requireCompilerInvariant(line->section, "inferred code line has no owning section");
		bool lineBelongsToInferenceRoot =
			line->section == inferenceRootSection || line->section->isDescendantOf(inferenceRootSection);
		if (!context.trial && lineGroupingAmbiguityIncomplete && lineBelongsToInferenceRoot) {
			auto existing = std::find_if(
				context.parseContext.deferredGroupingAmbiguities.begin(),
				context.parseContext.deferredGroupingAmbiguities.end(),
				[&](const ParseContext::DeferredGroupingAmbiguity &deferred) {
				return deferred.line == line;
			}
			);
			if (existing == context.parseContext.deferredGroupingAmbiguities.end()) {
				context.parseContext.deferredGroupingAmbiguities.push_back({
					line,
					inferenceRootSection,
					context.currentInstantiation,
					context.captureInferenceTraceRelatedInfo(lineExpression),
				});
			}
		}
		bool ambiguityChecked = detectLineGroupingAmbiguity && !lineGroupingAmbiguityIncomplete;
		commitCodeLineGrouping(line, lineExpression, context, ambiguityChecked);
		return true;
	};

	for (size_t i = 0; i < section->codeLines.size(); i++) {
		CodeLine *line = section->codeLines[i];
		Expression *&lineExpression = body ? body->lineExpression(i) : line->expression;
		auto constantsBeforeLine = context.currentKnownConstants;
		if (!inferLineExpression(line, lineExpression))
			return false;
		auto headerInfo = controlHeaderInfo(line, lineExpression);
		if (headerInfo && headerInfo->first == "if") {
			auto constantsBeforeChain = std::move(constantsBeforeLine);
			size_t chainEnd = i;
			while (chainEnd + 1 < section->codeLines.size()) {
				CodeLine *next = section->codeLines[chainEnd + 1];
				if (!next->sectionOpening || dynamic_cast<DefinitionSection *>(next->sectionOpening))
					break;
				Expression *&nextExpression = body ? body->lineExpression(chainEnd + 1) : next->expression;
				applyCodeLineGrouping(next, nextExpression, context);
				std::optional<std::string> nextKind = finalizedControlHeaderKind(nextExpression, context);
				if (!nextKind) {
					nextKind = inferControlHeaderKindForLookahead(
						nextExpression, context, codeLineCanReuseGrouping(next, context), bindingFrameStack
					);
				}
				if (!nextKind)
					break;
				if (*nextKind != "else if" && *nextKind != "else")
					break;
				chainEnd++;
			}

			for (size_t k = i + 1; k <= chainEnd; k++) {
				CodeLine *header = section->codeLines[k];
				Expression *&headerExpression = body ? body->lineExpression(k) : header->expression;
				if (!inferLineExpression(header, headerExpression))
					return false;
			}

			std::optional<size_t> selectedBranch;
			bool branchKnown = true;
			for (size_t k = i; k <= chainEnd; k++) {
				Expression *branchExpression = body ? body->lineExpression(k) : section->codeLines[k]->expression;
				auto branchInfo = controlHeaderInfo(section->codeLines[k], branchExpression);
				if (!branchInfo) {
					branchKnown = false;
					break;
				}
				const std::string &branchKind = branchInfo->first;
				Expression *header = branchInfo->second;
				if (branchKind == "else") {
					if (!selectedBranch.has_value())
						selectedBranch = k;
					break;
				}
				if (header->arguments.size() < 2) {
					branchKnown = false;
					break;
				}
				CompileTimeValue conditionValue = context.lookupExpressionValue(header->arguments[1]);
				auto *condition = std::get_if<bool>(&conditionValue);
				if (!condition) {
					branchKnown = false;
					break;
				}
				if (*condition) {
					selectedBranch = k;
					break;
				}
			}

			if (!context.trial) {
				Expression *&firstHeaderExpression = body ? body->lineExpression(i) : line->expression;
				requireCompilerInvariant(firstHeaderExpression, "if chain is missing its inferred header expression");
				firstHeaderExpression->branchSelection = Expression::BranchSelection{
					.known = branchKnown,
					.selectedBranchIndex = branchKnown && selectedBranch.has_value() ? static_cast<int>(*selectedBranch) : -1,
				};
			}

			if (branchKnown && selectedBranch.has_value()) {
				context.currentKnownConstants = constantsBeforeChain;
				if (!inferOpenedSection(section->codeLines[*selectedBranch]))
					return false;
			} else {
				std::vector<std::unordered_map<VariableReference *, CompileTimeValue>> branchStates;
				bool hasElseBranch = false;
				for (size_t k = i; k <= chainEnd; k++) {
					Expression *branchExpression = body ? body->lineExpression(k) : section->codeLines[k]->expression;
					auto branchInfo = controlHeaderInfo(section->codeLines[k], branchExpression);
					if (branchInfo && branchInfo->first == "else")
						hasElseBranch = true;
					context.currentKnownConstants = constantsBeforeChain;
					if (!inferOpenedSection(section->codeLines[k]))
						return false;
					branchStates.push_back(context.currentKnownConstants);
				}
				if (!hasElseBranch)
					branchStates.push_back(constantsBeforeChain);
				std::unordered_map<VariableReference *, CompileTimeValue> mergedConstants =
					branchStates.empty() ? constantsBeforeChain : branchStates.front();
				for (size_t idx = 1; idx < branchStates.size(); ++idx) {
					const auto &other = branchStates[idx];
					for (auto it = mergedConstants.begin(); it != mergedConstants.end();) {
						auto otherIt = other.find(it->first);
						if (otherIt == other.end() || otherIt->second != it->second) {
							it = mergedConstants.erase(it);
						} else {
							++it;
						}
					}
				}
				context.currentKnownConstants = std::move(mergedConstants);
			}

			i = chainEnd;
			continue;
		}

		if (!inferOpenedSection(line))
			return false;
	}
	if (loopSection) {
		std::unordered_set<VariableReference *> loopMutations = loopMutationScope.finish();
		bool needsLoopReinference = false;
		for (VariableReference *ref : loopMutations) {
			if (constantsAtLoopEntry.contains(ref)) {
				needsLoopReinference = true;
				break;
			}
		}
		if (needsLoopReinference) {
			context.currentKnownConstants = constantsAtLoopEntry;
			for (VariableReference *ref : loopMutations)
				context.setKnownConstant(ref, {});
			return inferSection(section, body, openingExpression, context, bindingFrameStack);
		}
	}
	context.typesValid = true;
	return true;
}

struct PatternTypeConstraintWorkItem {
	PatternDefinition *definition{};
	DefinitionPatternElement *element{};
	Expression *expression{};
};

enum class PatternTypeConstraintProbe { Ready, Deferred, Invalid, Impure };

static bool readPatternTypeConstraintValue(
	Expression *expression, InferenceContext &context, TypeConstraint &outConstraint, DataType &outParameterType
) {
	outParameterType = {};
	CompileTimeValue value = resolveStoredCompileTimeValue(expression, {}, &context);
	if (const auto *constraint = std::get_if<TypeConstraint>(&value)) {
		if (!constraint->isResolved())
			return false;
		outConstraint = *constraint;
		outParameterType = constraint->exactValueType().value_or(DataType{});
		return true;
	}
	if (const auto *typeReference = std::get_if<TypeReferenceValue>(&value)) {
		if (!typeReference->constraint.isResolved())
			return false;
		outConstraint = typeReference->constraint;
		if (typeReference->type.kind == DataType::Kind::Type)
			outParameterType = typeReference->type.toReferencedType();
		return true;
	}
	if (expression && expression->type.kind == DataType::Kind::Type &&
		expression->type.referencedKind != DataType::Kind::Unresolved) {
		outConstraint = TypeConstraint::fromTypeReference(expression->type);
		outParameterType = expression->type.toReferencedType();
		return true;
	}
	return false;
}

static PatternTypeConstraintProbe probePatternTypeConstraint(PatternTypeConstraintWorkItem &item, ParseContext &parseContext) {
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

static void commitPatternTypeConstraint(PatternTypeConstraintWorkItem &item, ParseContext &parseContext) {
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
	item.element->resolvedTypeConstraint = std::move(constraint);
	item.element->resolvedParameterType = std::move(parameterType);
}

static void collectPatternTypeConstraintWorkItems(
	ParseContext &parseContext, Section *section, std::vector<PatternTypeConstraintWorkItem> &items
) {
	for (PatternDefinition *definition : section->patternDefinitions) {
		std::function<void(std::vector<DefinitionPatternElement> &)> collectElements =
			[&](std::vector<DefinitionPatternElement> &elements) {
			for (DefinitionPatternElement &element : elements) {
				if (element.type == PatternElement::Type::Choice) {
					for (auto &alternative : element.alternatives)
						collectElements(alternative);
					continue;
				}
				if (element.typeConstraintName.empty() || element.resolvedTypeConstraint.isResolved())
					continue;
				int constraintEnd = definition->range.start() + static_cast<int>(element.startPos) - 1;
				int constraintStart = constraintEnd - static_cast<int>(element.typeConstraintName.size());
				Range constraintRange(definition->range.line, constraintStart, constraintEnd);
				items.push_back({
					definition,
					&element,
					createTypeConstraintExpression(parseContext, definition->section, constraintRange),
				});
			}
		};
		collectElements(definition->patternElements);
	}
	for (Section *child : section->children)
		collectPatternTypeConstraintWorkItems(parseContext, child, items);
}

static bool inferPatternTypeConstraints(ParseContext &parseContext) {
	std::vector<PatternTypeConstraintWorkItem> items;
	size_t diagnosticsBeforeParsing = parseContext.diagnostics.size();
	collectPatternTypeConstraintWorkItems(parseContext, parseContext.mainSection, items);
	auto destroyExpressions = [&]() {
		for (PatternTypeConstraintWorkItem &item : items) {
			if (!item.expression)
				continue;
			destroyTypeConstraintExpression(item.expression);
			item.expression = nullptr;
		}
	};
	for (const PatternTypeConstraintWorkItem &item : items) {
		if (item.expression)
			continue;
		if (parseContext.diagnostics.size() == diagnosticsBeforeParsing) {
			parseContext.diagnostics.push_back(Diagnostic(
				parseContext, Diagnostic::Level::Error, "unknown type constraint", item.definition->range, "type_constraint",
				item.element->typeConstraintName
			));
		}
		destroyExpressions();
		return false;
	}

	size_t unresolvedCount = items.size();
	while (unresolvedCount > 0) {
		bool madeProgress = false;
		for (PatternTypeConstraintWorkItem &item : items) {
			if (!item.expression)
				continue;
			if (!item.definition || !item.element) {
				destroyExpressions();
				crashCompilerBug("pattern type-constraint work item lost its source definition");
			}
			PatternTypeConstraintProbe probe = probePatternTypeConstraint(item, parseContext);
			if (probe == PatternTypeConstraintProbe::Deferred)
				continue;
			if (probe == PatternTypeConstraintProbe::Invalid || probe == PatternTypeConstraintProbe::Impure) {
				const char *diagnosticKey =
					probe == PatternTypeConstraintProbe::Impure ? "impure type constraint" : "unknown type constraint";
				parseContext.diagnostics.push_back(Diagnostic(
					parseContext, Diagnostic::Level::Error, diagnosticKey, item.definition->range, "type_constraint",
					item.element->typeConstraintName
				));
				destroyExpressions();
				return false;
			}
			commitPatternTypeConstraint(item, parseContext);
			destroyTypeConstraintExpression(item.expression);
			item.expression = nullptr;
			unresolvedCount--;
			madeProgress = true;
		}
		if (madeProgress)
			continue;
		auto unresolved = std::find_if(items.begin(), items.end(), [](const PatternTypeConstraintWorkItem &item) {
			return item.expression != nullptr;
		});
		requireCompilerInvariant(unresolved != items.end(), "type-constraint worklist lost its unresolved item");
		parseContext.diagnostics.push_back(Diagnostic(
			parseContext, Diagnostic::Level::Error, "cyclic type constraint", unresolved->definition->range, "type_constraint",
			unresolved->element->typeConstraintName
		));
		destroyExpressions();
		return false;
	}
	return true;
}

#include "grouping_finalization.inl"

bool inferTypes(ParseContext &parseContext) {
	ActiveTypeResolutionParseContextGuard typeResolutionGuard(parseContext);
	if (!inferPatternTypeConstraints(parseContext))
		return false;
	if (!validatePatternDefinitionConflicts(parseContext))
		return false;
	InferenceContext context(parseContext);
	context.currentKnownConstants.clear();
	if (!inferSection(parseContext.mainSection, nullptr, nullptr, context, {}))
		return false;
	std::function<bool(Section *)> inferExposedFunctions = [&](Section *section) {
		if (!section)
			return true;
		if (section->isExposed) {
			if (section->patternDefinitions.empty() ||
				!ensureCallableFunctionInstantiationInferred(
					section->patternDefinitions.front(), context,
					section->openingLine ? Range(section->openingLine, section->openingLine->patternText) : Range()
				)) {
				return false;
			}
		}
		for (Section *child : section->children) {
			if (!inferExposedFunctions(child))
				return false;
		}
		return true;
	};
	if (!inferExposedFunctions(parseContext.mainSection))
		return false;
	if (!finalizeDeferredGroupingAmbiguities(parseContext))
		return false;

	// Validate variables — all must have deduced types
	// Skip definition body sections: their variables are inferred when the definition is instantiated/expanded.
	bool valid = true;
	std::function<void(Section *)> validateVariables = [&](Section *section) {
		if (!section->patternDefinitions.empty())
			return;
		if (section->parent && !section->parent->patternDefinitions.empty())
			return;
		for (auto &[name, var] : section->variables) {
			if (!var->type.isDeduced()) {
				parseContext.diagnostics.push_back(Diagnostic(
					parseContext, Diagnostic::Level::Error, "variable has no type", var->definition->range, "name", name
				));
				valid = false;
			}
		}
		for (Section *child : section->children)
			validateVariables(child);
	};
	validateVariables(parseContext.mainSection);

	// Validate non-flex functions have deduced return types
	std::function<void(Section *)> validateReturnTypes = [&](Section *section) {
		if (section->type == SectionType::Function && !section->isFlex && !section->patternDefinitions.empty()) {
			for (auto &[argTypes, inst] : section->instantiations) {
				(void)argTypes;
				if (!inst.valid)
					continue;
				if (!inst.returnType.isDeduced()) {
					parseContext.diagnostics.push_back(Diagnostic(
						parseContext, Diagnostic::Level::Error, "function has no deduced return type",
						section->patternDefinitions.front()->range, "function",
						(std::string)section->patternDefinitions.front()->range.subString
					));
					valid = false;
					break; // one error per section is enough
				}
			}
		}
		for (Section *child : section->children)
			validateReturnTypes(child);
	};
	validateReturnTypes(parseContext.mainSection);

	return valid;
}

bool ensureSectionInstantiationInferred(
	ParseContext &parseContext, Section *section, PatternDefinition *definition,
	const std::vector<std::pair<std::string, Expression *>> &paramBindings, const std::vector<DataType> &argTypes,
	const std::unordered_set<std::string> &explicitCompileTimeParameters, const Instantiation *callerInstantiation,
	InferenceContext *callerContext
) {
	ActiveTypeResolutionParseContextGuard typeResolutionGuard(parseContext);
	if (!section)
		return false;
	(void)definition;
	(void)callerInstantiation;
	auto evaluateParameterValue = [&](Expression *argumentExpression) {
		if (!argumentExpression)
			crashCompilerBug("missing section parameter expression while building instantiation key");
		return callerContext ? callerContext->lookupExpressionValue(argumentExpression)
							 : getExpressionCompileTimeValue(argumentExpression);
	};
	InstantiationKey instantiationKey =
		findMatchingInstantiationKey(section, paramBindings, argTypes, evaluateParameterValue)
			.value_or(buildInstantiationKey(explicitCompileTimeParameters, paramBindings, argTypes, evaluateParameterValue));
	if (callerContext && callerContext->trial) {
		if (!callerContext->trialJournal)
			crashCompilerBug("trial section-instantiation inference started without an active trial journal");
		callerContext->trialJournal->recordSectionInstantiationWrite(section, instantiationKey);
	}
	auto instIt = section->instantiations.find(instantiationKey);
	if (instIt == section->instantiations.end())
		instIt = section->instantiations.emplace(instantiationKey, Instantiation{}).first;
	Instantiation &inst = instIt->second;
	if (!inst.body)
		inst.body = parseContext.cloneSectionBody(section);
	if (inst.argumentTypes.empty())
		inst.argumentTypes = argTypes;
	else
		requireCompilerInvariant(inst.argumentTypes == argTypes, "Instantiation argumentTypes diverged from map key");
	inst.requiredCompileTimeParameters = explicitCompileTimeParameters;
	seedInstantiationParameterTypes(inst, paramBindings, argTypes);
	seedInstantiationCompileTimeParameters(
		inst, paramBindings, argTypes, inst.requiredCompileTimeParameters,
		[&](Expression *argumentExpression) {
		return callerContext ? callerContext->lookupExpressionValue(argumentExpression)
							 : getExpressionCompileTimeValue(argumentExpression);
	}
	);
	if (inst.returnType.isDeduced() && !inst.needsReinfer)
		return inst.valid;
	if (inst.inferring)
		return inst.returnType.isDeduced() && inst.valid;
	ScopedSectionLocalVariableState calleeVariableState(section);

	inst.writtenGlobalReferences.clear();
	inst.finalGlobalConstantValues.clear();
	inst.purity = InstantiationPurity::Pure;
	inst.pureReturnValuesByArguments.clear();
	InferenceContext context(parseContext, callerContext && callerContext->trial);
	if (callerContext) {
		// Do not seed from the caller's tracked constants: the instantiation is
		// cached and reused under other global states (see the non-flex call
		// path in function_inference.inl).
		context.allowTrialSummaryReuse = callerContext->allowTrialSummaryReuse;
		context.inheritedTrialExpressionValues =
			callerContext->trial ? &callerContext->trialExpressionValues : callerContext->inheritedTrialExpressionValues;
		context.trialJournal = callerContext->trialJournal;
		context.trialInstantiationCache =
			callerContext->trialInstantiationCache
				? callerContext->trialInstantiationCache
				: (callerContext->trial ? callerContext->ensureTrialInstantiationCache() : nullptr);
		context.unresolvedPatternConstraintSignal = callerContext->unresolvedPatternConstraintSignal;
		context.suppressDiagnostics = callerContext->suppressDiagnostics;
		context.suppressReinferPassDiagnostics = callerContext->suppressReinferPassDiagnostics;
	}
	inst.inferring = true;
	Instantiation *savedInst = context.currentInstantiation;
	for (const auto &[name, value] : inst.constantParameterValues) {
		Variable *var = findOwnSectionVariable(section, name);
		if (var)
			context.setKnownConstant(var->definition, value);
	}
	context.currentInstantiation = &inst;
	if (context.trial)
		inst.body = parseContext.cloneSectionBody(section);
	bool inferenceSucceeded = inferSection(section, inst.body.get(), nullptr, context, {});
	for (VariableReference *reference : inst.writtenGlobalReferences) {
		auto knownIt = context.currentKnownConstants.find(reference);
		if (knownIt != context.currentKnownConstants.end() && isCompileTimeKnown(knownIt->second))
			inst.finalGlobalConstantValues[reference] = knownIt->second;
	}
	context.currentInstantiation = savedInst;
	inst.inferring = false;
	inst.valid = inferenceSucceeded;
	if (callerContext) {
		callerContext->observedInProgressUndeducedInstantiation =
			callerContext->observedInProgressUndeducedInstantiation || context.observedInProgressUndeducedInstantiation;
		callerContext->groupingAmbiguityIncomplete =
			callerContext->groupingAmbiguityIncomplete || context.groupingAmbiguityIncomplete;
		callerContext->inheritTypeFailureFrom(context);
	}
	if (!inst.valid || !context.typesValid)
		return false;

	bool unresolvedLocalFound = false;
	std::function<void(Section *)> validateInstantiatedVariables = [&](Section *currentSection) {
		if (unresolvedLocalFound || !currentSection)
			return;
		for (auto &[name, var] : currentSection->variables) {
			if (var->type.isDeduced())
				continue;
			if (!context.suppressReinferPassDiagnostics) {
				context.addDiagnostic(Diagnostic(
					parseContext, Diagnostic::Level::Error, "variable has no type", var->definition->range, "name", name
				));
			}
			unresolvedLocalFound = true;
			return;
		}
		for (Section *child : currentSection->children)
			validateInstantiatedVariables(child);
	};
	validateInstantiatedVariables(section);
	if (unresolvedLocalFound) {
		inst.valid = false;
		return false;
	}

	InstantiationKey refinedKey =
		buildInstantiationKey(inst.requiredCompileTimeParameters, paramBindings, argTypes, evaluateParameterValue);
	if (refinedKey != instIt->first) {
		retargetTrialSectionInstantiationWriteOrCrash(
			context, section, instantiationKey, refinedKey, "trial section-instantiation inference"
		);
		auto node = section->instantiations.extract(instIt);
		node.key() = refinedKey;
		auto insertResult = section->instantiations.insert(std::move(node));
		requireCompilerInvariant(insertResult.inserted, "Refined instantiation key collided with existing entry");
	}

	if (!inst.needsReinfer && !context.observedInProgressUndeducedInstantiation && inst.returnType.kind == DataType::Kind::Any)
		inst.returnType = {DataType::Kind::Void};

	return inst.returnType.isDeduced();
}
