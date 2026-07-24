#pragma once

#include "operand_reordering.inl"
#include "section_inference_helpers.inl"

static bool inferSectionLineRange(
	Section *section, InstantiatedSectionBody *body, Expression *openingExpression, InferenceContext &context,
	const BindingFrameStack &bindingFrameStack, size_t startLineIndex, bool initializeSection, bool *fallsThrough,
	bool stabilizeLoop = true
) {
	requireCompilerInvariant(!body || body->sourceSection == section, "active instantiated body does not match section");
	InstantiatedSectionBody *savedInstantiatedBody = context.currentInstantiatedSectionBody;
	context.currentInstantiatedSectionBody = body;
	struct InstantiatedBodyRestore {
		InferenceContext &context;
		InstantiatedSectionBody *savedBody;
		~InstantiatedBodyRestore() { context.currentInstantiatedSectionBody = savedBody; }
	} instantiatedBodyRestore{context, savedInstantiatedBody};
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
	resetSectionExpressionTypes(section, body, startLineIndex);
	if (initializeSection)
		resetSectionLocalVariableTypes(section);
	if (initializeSection && (section->isFlex || section->patternDefinitions.empty())) {
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
			context.setAddressProvenance(boundVar->definition, inferAddressProvenance(boundExpr, context, bindingFrameStack));
		}
	} else if (initializeSection) {
		seedNonFlexSectionParameterState(section, context);
	}

	bool loopSection = stabilizeLoop && sectionOutcomeIsLoop(openingExpression);
	std::unordered_map<VariableReference *, CompileTimeValue> constantsAtLoopEntry;
	AddressInferenceState addressesAtLoopEntry;
	InferenceContext::SubjectState subjectAtLoopEntry;
	if (loopSection) {
		constantsAtLoopEntry = context.currentVariableValues;
		addressesAtLoopEntry = context.currentAddressState;
		subjectAtLoopEntry = context.currentSubject;
	}

	auto inferOpenedSection = [&](CodeLine *line, bool *openedSectionFallsThrough = nullptr) {
		if (!line || !line->sectionOpening || dynamic_cast<DefinitionSection *>(line->sectionOpening))
			return true;
		InstantiatedSectionBody *openedBody = body ? body->bodyForChild(line->sectionOpening) : nullptr;
		Expression *activeOpeningExpression =
			body && line->sectionOpening ? body->findCloneOf(line->expression) : line->expression;
		if (activeOpeningExpression && activeOpeningExpression->sectionBodyInferred) {
			if (openedSectionFallsThrough)
				*openedSectionFallsThrough = activeOpeningExpression->sectionBodyFallsThrough;
			return true;
		}
		if (!inferSection(
				line->sectionOpening, openedBody, activeOpeningExpression, context, bindingFrameStack, openedSectionFallsThrough
			)) {
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
		bool ambiguityChecked = context.detectGroupingAmbiguity;
		if (!inferExpression(
				lineExpression, context, alreadyOrdered, bindingFrameStack, section->type != SectionType::Replacement
			)) {
			context.typesValid = false;
			return false;
		}
		commitCodeLineGrouping(line, lineExpression, context, ambiguityChecked);
		lineExpression->executionFallsThrough = true;
		return true;
	};

	if (openingExpression && openingExpression->sectionOutcome.kind == Expression::SectionOutcome::Kind::Switch) {
		struct SwitchBranch {
			size_t lineIndex;
			std::optional<std::int64_t> value;
			bool isDefault;
		};
		auto entryConstants = context.currentVariableValues;
		auto entryAddresses = context.currentAddressState;
		InferenceContext::SubjectState entrySubject = context.currentSubject;
		std::vector<SwitchBranch> branches;
		std::optional<size_t> defaultBranch;
		std::unordered_set<std::int64_t> caseValues;
		DataType selectorType = openingExpression->sectionOutcome.conditionType;
		requireCompilerInvariant(selectorType.isInteger(), "inferred switch outcome has no integer selector type");
		for (size_t index = 0; index < section->codeLines.size(); index++) {
			CodeLine *line = section->codeLines[index];
			Expression *&lineExpression = body ? body->lineExpression(index) : line->expression;
			if (!inferLineExpression(line, lineExpression))
				return false;
			Expression::SectionOutcome::Kind kind =
				lineExpression ? lineExpression->sectionOutcome.kind : Expression::SectionOutcome::Kind::None;
			if (kind != Expression::SectionOutcome::Kind::Case && kind != Expression::SectionOutcome::Kind::DefaultCase) {
				context.fail(Diagnostic(
					context.parseContext, Diagnostic::Level::Error, "switch body requires case",
					lineExpression ? lineExpression->range : Range(line, line->patternText)
				));
				return false;
			}
			bool isDefault = kind == Expression::SectionOutcome::Kind::DefaultCase;
			std::optional<std::int64_t> caseValue;
			if (!isDefault) {
				caseValue = getCompileTimeIntegerValue(lineExpression->sectionOutcome.conditionValue);
				if (!caseValue) {
					context.fail(Diagnostic(
						context.parseContext, Diagnostic::Level::Error, "case value must be constant integer",
						lineExpression->range
					));
					return false;
				}
				caseValue = normalizeSignedIntegerToType(*caseValue, selectorType);
			}
			if (isDefault) {
				if (defaultBranch) {
					context.fail(Diagnostic(
						context.parseContext, Diagnostic::Level::Error, "duplicate default case", lineExpression->range
					));
					return false;
				}
				defaultBranch = branches.size();
			} else if (!caseValues.insert(*caseValue).second) {
				context.fail(Diagnostic(context.parseContext, Diagnostic::Level::Error, "duplicate case", lineExpression->range)
				);
				return false;
			}
			branches.push_back({index, caseValue, isDefault});
		}

		std::optional<std::int64_t> selector = getCompileTimeIntegerValue(openingExpression->sectionOutcome.conditionValue);
		if (selector)
			selector = normalizeSignedIntegerToType(*selector, selectorType);
		bool selectionKnown = selector.has_value();
		std::optional<size_t> selectedBranch;
		if (selectionKnown) {
			for (size_t index = 0; index < branches.size(); index++) {
				if (!branches[index].isDefault && branches[index].value == selector) {
					selectedBranch = index;
					break;
				}
			}
			if (!selectedBranch)
				selectedBranch = defaultBranch;
		}

		std::vector<std::unordered_map<VariableReference *, CompileTimeValue>> fallthroughConstantStates;
		std::vector<AddressInferenceState> fallthroughAddressStates;
		std::vector<InferenceContext::SubjectState> fallthroughSubjectStates;
		auto inferBranch = [&](const SwitchBranch &branch) {
			context.currentVariableValues = entryConstants;
			context.currentAddressState = entryAddresses;
			context.currentSubject = entrySubject;
			bool branchFallsThrough = true;
			if (!inferOpenedSection(section->codeLines[branch.lineIndex], &branchFallsThrough))
				return false;
			if (branchFallsThrough) {
				fallthroughConstantStates.push_back(context.currentVariableValues);
				fallthroughAddressStates.push_back(context.currentAddressState);
				fallthroughSubjectStates.push_back(context.currentSubject);
			}
			return true;
		};
		for (size_t index = 0; index < branches.size(); index++) {
			Expression *&branchExpression = body ? body->lineExpression(branches[index].lineIndex)
												 : section->codeLines[branches[index].lineIndex]->expression;
			bool reachable = !selectionKnown || selectedBranch == index;
			branchExpression->sectionBodyReachable = reachable;
			if (reachable && !inferBranch(branches[index]))
				return false;
		}
		if ((selectionKnown && !selectedBranch) || (!selectionKnown && !defaultBranch)) {
			fallthroughConstantStates.push_back(entryConstants);
			fallthroughAddressStates.push_back(entryAddresses);
			fallthroughSubjectStates.push_back(entrySubject);
		}
		bool sectionFallsThrough = !fallthroughConstantStates.empty();
		openingExpression->branchSelection = Expression::BranchSelection{
			.known = selectionKnown,
			.selectedBranchIndex =
				selectionKnown && selectedBranch ? static_cast<int>(branches[*selectedBranch].lineIndex) : -1,
		};
		openingExpression->executionFallsThrough = sectionFallsThrough;
		if (sectionFallsThrough)
			mergeSectionExecutionStates(context, fallthroughConstantStates, fallthroughAddressStates, fallthroughSubjectStates);
		if (fallsThrough)
			*fallsThrough = sectionFallsThrough;
		context.typesValid = true;
		return true;
	}

	bool sectionFallsThrough = true;
	for (size_t i = startLineIndex; i < section->codeLines.size(); i++) {
		CodeLine *line = section->codeLines[i];
		Expression *&lineExpression = body ? body->lineExpression(i) : line->expression;
		auto constantsBeforeLine = context.currentVariableValues;
		auto addressesBeforeLine = context.currentAddressState;
		auto subjectBeforeLine = context.currentSubject;
		if (!inferLineExpression(line, lineExpression))
			return false;
		if (lineExpression && lineExpression->sectionOutcome.kind == Expression::SectionOutcome::Kind::FunctionReturn) {
			lineExpression->executionFallsThrough = false;
			sectionFallsThrough = false;
			break;
		}
		if (lineExpression && lineExpression->sectionOutcome.kind == Expression::SectionOutcome::Kind::Conditional) {
			auto constantsBeforeChain = std::move(constantsBeforeLine);
			auto addressesBeforeChain = std::move(addressesBeforeLine);
			auto subjectBeforeChain = subjectBeforeLine;
			size_t chainEnd = i;
			std::optional<size_t> selectedBranch;
			bool branchKnown = true;
			bool fallthroughReachable = true;
			std::unordered_map<VariableReference *, CompileTimeValue> fallthroughConstants = context.currentVariableValues;
			AddressInferenceState fallthroughAddresses = context.currentAddressState;
			InferenceContext::SubjectState fallthroughSubject = context.currentSubject;
			std::vector<std::unordered_map<VariableReference *, CompileTimeValue>> branchConstantStates;
			std::vector<AddressInferenceState> branchAddressStates;
			std::vector<InferenceContext::SubjectState> branchSubjectStates;
			for (size_t k = i;; k++) {
				CodeLine *branchLine = section->codeLines[k];
				Expression *&branchExpression = body ? body->lineExpression(k) : branchLine->expression;
				if (k > i) {
					context.currentVariableValues = fallthroughReachable ? fallthroughConstants : constantsBeforeChain;
					context.currentAddressState = fallthroughReachable ? fallthroughAddresses : addressesBeforeChain;
					context.currentSubject = fallthroughReachable ? fallthroughSubject : subjectBeforeChain;
					if (!inferLineExpression(branchLine, branchExpression))
						return false;
				}
				Expression::SectionOutcome outcome = branchExpression->sectionOutcome;
				requireCompilerInvariant(
					(k == i && outcome.kind == Expression::SectionOutcome::Kind::Conditional) ||
						(k > i && (outcome.kind == Expression::SectionOutcome::Kind::AlternativeConditional ||
								   outcome.kind == Expression::SectionOutcome::Kind::Alternative)),
					"branch chain contains an invalid section outcome"
				);
				bool bodyReachable = false;
				if (fallthroughReachable) {
					if (outcome.kind == Expression::SectionOutcome::Kind::Alternative) {
						bodyReachable = true;
						fallthroughReachable = false;
						if (branchKnown)
							selectedBranch = k;
					} else if (const auto *condition = std::get_if<bool>(&outcome.conditionValue)) {
						if (*condition) {
							bodyReachable = true;
							fallthroughReachable = false;
							if (branchKnown)
								selectedBranch = k;
						}
					} else {
						bodyReachable = true;
						branchKnown = false;
					}
				}
				branchExpression->sectionBodyReachable = bodyReachable;
				auto branchEntryConstants = context.currentVariableValues;
				auto branchEntryAddresses = context.currentAddressState;
				auto branchEntrySubject = context.currentSubject;
				if (bodyReachable) {
					bool bodyFallsThrough = true;
					if (!inferOpenedSection(branchLine, &bodyFallsThrough))
						return false;
					if (bodyFallsThrough) {
						branchConstantStates.push_back(context.currentVariableValues);
						branchAddressStates.push_back(context.currentAddressState);
						branchSubjectStates.push_back(context.currentSubject);
					}
				}
				context.currentVariableValues = std::move(branchEntryConstants);
				context.currentAddressState = std::move(branchEntryAddresses);
				context.currentSubject = branchEntrySubject;
				if (fallthroughReachable) {
					fallthroughConstants = context.currentVariableValues;
					fallthroughAddresses = context.currentAddressState;
					fallthroughSubject = context.currentSubject;
				}
				chainEnd = k;
				if (k + 1 >= section->codeLines.size())
					break;
				CodeLine *next = section->codeLines[k + 1];
				if (!next->sectionOpening || dynamic_cast<DefinitionSection *>(next->sectionOpening))
					break;
				Expression *&nextExpression = body ? body->lineExpression(k + 1) : next->expression;
				applyCodeLineGrouping(next, nextExpression, context);
				std::optional<Expression::SectionOutcome::Kind> nextOutcome = finalizedBranchOutcome(nextExpression);
				if (!nextOutcome) {
					nextOutcome = inferExpressionSectionOutcomeInTrial(
						nextExpression, context, codeLineCanReuseGrouping(next, context), bindingFrameStack
					);
				}
				if (!nextOutcome || (*nextOutcome != Expression::SectionOutcome::Kind::AlternativeConditional &&
									 *nextOutcome != Expression::SectionOutcome::Kind::Alternative)) {
					break;
				}
			}

			Expression *&firstHeaderExpression = body ? body->lineExpression(i) : line->expression;
			requireCompilerInvariant(firstHeaderExpression, "if chain is missing its inferred header expression");
			if (fallthroughReachable) {
				branchConstantStates.push_back(std::move(fallthroughConstants));
				branchAddressStates.push_back(std::move(fallthroughAddresses));
				branchSubjectStates.push_back(fallthroughSubject);
			}
			bool chainFallsThrough = !branchConstantStates.empty();
			firstHeaderExpression->branchSelection = Expression::BranchSelection{
				.known = branchKnown,
				.selectedBranchIndex = branchKnown && selectedBranch.has_value() ? static_cast<int>(*selectedBranch) : -1,
			};
			firstHeaderExpression->executionFallsThrough = chainFallsThrough;
			if (!chainFallsThrough) {
				sectionFallsThrough = false;
				i = chainEnd;
				break;
			}
			mergeSectionExecutionStates(context, branchConstantStates, branchAddressStates, branchSubjectStates);

			i = chainEnd;
			continue;
		}
		if (lineExpression && lineExpression->sectionOutcome.kind == Expression::SectionOutcome::Kind::Loop) {
			const bool *condition = std::get_if<bool>(&lineExpression->sectionOutcome.conditionValue);
			if (!line->sectionOpening) {
				sectionFallsThrough = true;
				if ((!condition || *condition) && i + 1 < section->codeLines.size() &&
					!inferSectionLineRange(
						section, body, lineExpression, context, bindingFrameStack, i + 1, false, &sectionFallsThrough
					)) {
					return false;
				}
				lineExpression->executionFallsThrough = sectionFallsThrough;
				break;
			}
			if (condition && !*condition) {
				lineExpression->sectionBodyReachable = false;
				continue;
			}
			bool loopFallsThrough = true;
			if (!inferOpenedSection(line, &loopFallsThrough))
				return false;
			lineExpression->executionFallsThrough = loopFallsThrough;
			if (!loopFallsThrough) {
				sectionFallsThrough = false;
				break;
			}
			continue;
		}
		bool openedSectionFallsThrough = true;
		if (!inferOpenedSection(line, &openedSectionFallsThrough))
			return false;
		if (lineExpression)
			lineExpression->executionFallsThrough = openedSectionFallsThrough;
		if ((!lineExpression || lineExpression->sectionOutcome.kind == Expression::SectionOutcome::Kind::None ||
			 lineExpression->sectionOutcome.kind == Expression::SectionOutcome::Kind::Switch) &&
			!openedSectionFallsThrough) {
			sectionFallsThrough = false;
			break;
		}
	}
	if (loopSection) {
		const bool *entryCondition = std::get_if<bool>(&openingExpression->sectionOutcome.conditionValue);
		bool firstIterationGuaranteed = entryCondition && *entryCondition;
		if (!sectionFallsThrough) {
			if (!firstIterationGuaranteed) {
				context.currentVariableValues = std::move(constantsAtLoopEntry);
				context.currentAddressState = std::move(addressesAtLoopEntry);
				context.currentSubject = subjectAtLoopEntry;
				sectionFallsThrough = true;
			}
		} else {
			auto constantsAfterFirstIteration = context.currentVariableValues;
			auto addressesAfterFirstIteration = context.currentAddressState;
			auto subjectAfterFirstIteration = context.currentSubject;
			const auto &baseConstants = firstIterationGuaranteed ? constantsAfterFirstIteration : constantsAtLoopEntry;
			const auto &baseAddresses = firstIterationGuaranteed ? addressesAfterFirstIteration : addressesAtLoopEntry;
			const auto &baseSubject = firstIterationGuaranteed ? subjectAfterFirstIteration : subjectAtLoopEntry;
			if (!firstIterationGuaranteed) {
				mergeSectionExecutionStates(
					context, {constantsAtLoopEntry, constantsAfterFirstIteration},
					{addressesAtLoopEntry, addressesAfterFirstIteration}, {subjectAtLoopEntry, subjectAfterFirstIteration}
				);
			}

			while (true) {
				Expression::SectionOutcome nextHeaderOutcome;
				if (!inferNextLoopHeaderOutcomeInTrial(openingExpression, context, bindingFrameStack, nextHeaderOutcome))
					return false;
				const bool *condition = std::get_if<bool>(&nextHeaderOutcome.conditionValue);
				if (condition && !*condition) {
					sectionFallsThrough = true;
					break;
				}

				auto constantsAtIterationEntry = context.currentVariableValues;
				auto addressesAtIterationEntry = context.currentAddressState;
				auto subjectAtIterationEntry = context.currentSubject;
				bool iterationFallsThrough = true;
				if (!inferSectionLineRange(
						section, body, openingExpression, context, bindingFrameStack, startLineIndex, false,
						&iterationFallsThrough, false
					)) {
					return false;
				}
				if (!iterationFallsThrough) {
					sectionFallsThrough = !condition;
					if (sectionFallsThrough) {
						context.currentVariableValues = std::move(constantsAtIterationEntry);
						context.currentAddressState = std::move(addressesAtIterationEntry);
						context.currentSubject = subjectAtIterationEntry;
					}
					break;
				}

				mergeSectionExecutionStates(
					context, {baseConstants, context.currentVariableValues}, {baseAddresses, context.currentAddressState},
					{baseSubject, context.currentSubject}
				);
				if (context.currentVariableValues == constantsAtIterationEntry &&
					context.currentAddressState == addressesAtIterationEntry &&
					context.currentSubject == subjectAtIterationEntry) {
					sectionFallsThrough = !condition;
					break;
				}
			}
			if (firstIterationGuaranteed && sectionFallsThrough)
				openingExpression->sectionOutcome.conditionValue = {};
		}
	}
	if (fallsThrough)
		*fallsThrough = sectionFallsThrough;
	context.typesValid = true;
	return true;
}

static bool inferSection(
	Section *section, InstantiatedSectionBody *body, Expression *openingExpression, InferenceContext &context,
	const BindingFrameStack &bindingFrameStack, bool *fallsThrough
) {
	return inferSectionLineRange(section, body, openingExpression, context, bindingFrameStack, 0, true, fallsThrough);
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

static bool inferManagedClassLifecycles(ParseContext &parseContext, InferenceContext &callerContext) {
	std::vector<ClassSection *> classes;
	std::function<void(Section *)> collectClasses = [&](Section *section) {
		if (!section)
			return;
		if (section->type == SectionType::Class)
			classes.push_back(static_cast<ClassSection *>(section));
		for (Section *child : section->children)
			collectClasses(child);
	};
	collectClasses(parseContext.mainSection);

	for (ClassSection *classSection : classes) {
		ClassDefinition *definition = classSection->classDefinition;
		requireCompilerInvariant(definition != nullptr, "class section is missing its class definition");
		if (static_cast<bool>(definition->retainSection) != static_cast<bool>(definition->releaseSection)) {
			parseContext.diagnostics.push_back(Diagnostic(
				parseContext, Diagnostic::Level::Error, "managed class requires both retain and release sections",
				classSection->openingLine ? Range(classSection->openingLine, classSection->openingLine->patternText) : Range()
			));
			return false;
		}
	}

	std::vector<size_t> inferredInstantiationCounts(classes.size(), 0);
	while (true) {
		bool madeProgress = false;
		for (size_t classIndex = 0; classIndex < classes.size(); classIndex++) {
			ClassSection *classSection = classes[classIndex];
			ClassDefinition *definition = classSection->classDefinition;
			if (!definition->retainSection)
				continue;
			while (inferredInstantiationCounts[classIndex] < definition->instantiations.size()) {
				size_t index = inferredInstantiationCounts[classIndex];
				DataType classType{DataType::Kind::Class};
				classType.classDefinition = definition;
				classType.classInstIndex = static_cast<int>(index);
				Expression placeholder;
				placeholder.kind = Expression::Kind::TypedPlaceholder;
				placeholder.type = classType;
				placeholder.range = classSection->openingLine
										? Range(classSection->openingLine, classSection->openingLine->patternText)
										: Range();
				std::vector<std::pair<std::string, Expression *>> bindings = {
					{std::string(managedLifecycleParameterName), &placeholder}
				};
				std::vector<DataType> argumentTypes = {classType};
				for (Section *lifecycleSection : {definition->retainSection, definition->releaseSection}) {
					if (!ensureSectionInstantiationInferred(
							parseContext, lifecycleSection, nullptr, bindings, argumentTypes, {},
							callerContext.currentInstantiation, &callerContext
						)) {
						return false;
					}
					InstantiationKey key{.argumentTypes = argumentTypes, .compileTimeParameters = {}};
					auto instantiation = lifecycleSection->instantiations.find(key);
					requireCompilerInvariant(
						instantiation != lifecycleSection->instantiations.end(),
						"managed lifecycle inference did not retain its instantiation"
					);
					if (instantiation->second.returnType.kind != DataType::Kind::Void) {
						parseContext.diagnostics.push_back(Diagnostic(
							parseContext, Diagnostic::Level::Error, "retain and release sections cannot return a value",
							lifecycleSection->openingLine
								? Range(lifecycleSection->openingLine, lifecycleSection->openingLine->patternText)
								: Range()
						));
						return false;
					}
				}
				inferredInstantiationCounts[classIndex]++;
				madeProgress = true;
			}
		}
		if (!madeProgress)
			break;
	}
	return true;
}

bool inferTypes(ParseContext &parseContext) {
	ActiveTypeResolutionParseContextGuard typeResolutionGuard(parseContext);
	if (!inferPatternTypeConstraints(parseContext))
		return false;
	if (!validatePatternDefinitionConflicts(parseContext))
		return false;
	InferenceContext context(parseContext);
	context.currentVariableValues.clear();
	context.currentAddressState = {};
	if (!inferSection(parseContext.mainSection, nullptr, nullptr, context, {})) {
		emitPendingInferenceFailure(context);
		return false;
	}
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
	if (!inferExposedFunctions(parseContext.mainSection)) {
		emitPendingInferenceFailure(context);
		return false;
	}
	if (!inferManagedClassLifecycles(parseContext, context)) {
		emitPendingInferenceFailure(context);
		return false;
	}

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
		for (Section *child : section->children) {
			Expression *openingExpression = child && child->openingLine ? child->openingLine->expression : nullptr;
			if (openingExpression && !openingExpression->sectionBodyReachable)
				continue;
			validateVariables(child);
		}
	};
	validateVariables(parseContext.mainSection);

	// Validate non-flex functions have deduced return types
	std::function<void(Section *)> validateReturnTypes = [&](Section *section) {
		if (section->type == SectionType::Function && !section->isFlex && !section->patternDefinitions.empty()) {
			PatternDefinition *definition = section->patternDefinitions.front();
			for (auto &[argTypes, inst] : section->instantiations) {
				(void)argTypes;
				if (!inst.valid)
					continue;
				if (!inst.returnType.isDeduced()) {
					parseContext.diagnostics.push_back(Diagnostic(
						parseContext, Diagnostic::Level::Error, "function has no deduced return type", definition->range,
						"function", (std::string)definition->range.subString
					));
					valid = false;
					break; // one error per section is enough
				}
				if (inst.returnType.kind != DataType::Kind::Void && inst.fallsThrough) {
					parseContext.diagnostics.push_back(Diagnostic(
						parseContext, Diagnostic::Level::Error, "function has missing return path", definition->range,
						"function", (std::string)definition->range.subString
					));
					valid = false;
					break;
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
	inst.finalGlobalAddressProvenance.clear();
	inst.addressTakenGlobalReferences.clear();
	inst.externallyEscapedGlobalProvenance = {};
	inst.returnAddressProvenance = {};
	inst.hasReturnAddressProvenance = false;
	inst.writesThroughUnknownAddress = false;
	inst.externallyEscapesUnknownAddress = false;
	inst.purity = InstantiationPurity::Pure;
	inst.pureReturnValuesByArguments.clear();
	InferenceContext context(parseContext, callerContext && callerContext->trial);
	if (callerContext) {
		// Do not seed from the caller's tracked constants: the instantiation is
		// cached and reused under other global states (see the non-flex call
		// path in function_inference.inl).
		context.inheritedTrialExpressionValues =
			callerContext->trial ? &callerContext->trialExpressionValues : callerContext->inheritedTrialExpressionValues;
		context.trialJournal = callerContext->trialJournal;
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
	bool instantiationFallsThrough = true;
	inst.needsReinfer = false;
	InstantiationInferencePassResult pass = runScopedInstantiationInferencePass(context, inst, [&]() {
		return inferSection(section, inst.body.get(), nullptr, context, {}, &instantiationFallsThrough);
	});
	bool inferenceSucceeded = pass.succeeded;
	inst.fallsThrough = instantiationFallsThrough;
	for (VariableReference *reference : inst.writtenGlobalReferences) {
		auto knownIt = context.currentVariableValues.find(reference);
		if (knownIt != context.currentVariableValues.end() && isCompileTimeKnown(knownIt->second))
			inst.finalGlobalConstantValues[reference] = knownIt->second;
		auto provenance = context.currentAddressState->variables.find(reference);
		inst.finalGlobalAddressProvenance[reference] = provenance != context.currentAddressState->variables.end()
														   ? provenance->second
														   : AddressProvenance{.mayTargets = {}, .unknown = true};
	}
	for (VariableReference *reference : context.currentAddressState->addressTakenVariables) {
		Variable *variable = variableForAddressTarget(reference);
		if (variable && variable->isGlobal)
			inst.addressTakenGlobalReferences.insert(reference);
	}
	for (VariableReference *reference : context.currentAddressState->externallyEscaped.mayTargets) {
		Variable *variable = variableForAddressTarget(reference);
		if (variable && variable->isGlobal)
			inst.externallyEscapedGlobalProvenance.mayTargets.insert(reference);
	}
	context.currentInstantiation = savedInst;
	inst.inferring = false;
	inst.valid = inferenceSucceeded;
	if (callerContext) {
		if (inferenceSucceeded && inst.needsReinfer)
			propagateUnresolvedRecursiveDependencyToCaller(*callerContext, callerInstantiation);
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

	if (!inst.needsReinfer && inst.returnType.kind == DataType::Kind::Any)
		inst.returnType = {DataType::Kind::Void};
	return inst.returnType.isDeduced();
}
