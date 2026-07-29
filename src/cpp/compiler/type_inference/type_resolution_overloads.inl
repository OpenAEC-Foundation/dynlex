#include "expression_invocation_identity.h"
#include "knownConstantState.h"

static bool mergeArrayElementType(const DataType &current, const DataType &next, DataType &merged) {
	if (!current.isDeduced() || !next.isDeduced())
		return false;
	if (current == next) {
		merged = current;
		return true;
	}
	if (current.isNumeric() && next.isNumeric())
		return DataType::promoteArithmetic(current, next, merged);
	return false;
}

// Wraps ParseContext with type validity tracking and trial mode for operand reordering.
// During reordering trials, diagnostics are suppressed and failures only affect the current trial.
struct InferenceContext {
	struct SubjectState {
		Expression *setter{};
		bool ambiguous = false;

		bool operator==(const SubjectState &) const = default;
	};

	struct OperandGroupingWarning {
		Range range;
		std::string expressionText;
		std::string chosenGrouping;
		std::string alternativeGrouping;
		std::vector<RelatedInfo> relatedInfo;
	};

	struct TrialCodeLineGrouping {
		GroupingSnapshot grouping;
		bool reusableTemplate = false;
		bool ambiguityChecked = false;
	};

	struct SectionFlexBodyInferenceFrame {
		Section *definitionSection{};
		InstantiatedSectionBody *definitionBody{};
		Section *bodySection{};
		InstantiatedSectionBody *instantiatedBody{};
		BindingFrameStack callerBindings;
		Expression *executeBodyCallSite{};
		std::optional<ExpressionInvocationIdentity> bodyTransferIdentity;
		bool bodyInferred = false;
		bool bodyFallsThrough = true;
	};

	struct TrialJournal {
		enum class SectionInstantiationRetargetResult {
			Updated,
			MissingSourceRecord,
			SourceWasPreexisting,
			TargetAlreadyRecorded,
		};

		struct VariableUndo {
			Variable *variable;
			DataType type;
			Range typeOriginRange;
			std::string typeOriginFloatLiteralReplacement;
		};

		struct SectionInstantiationUndo {
			Section *section;
			InstantiationKey key;
			bool existed;
			Instantiation value;
		};

		struct InstantiationUndo {
			Instantiation *instantiation;
			std::unique_ptr<Instantiation> value;
		};

		std::vector<VariableUndo> variableTypeUndo;
		std::unordered_set<Variable *> seenVariables;
		std::vector<std::pair<ClassDefinition *, size_t>> classInstantiationSizes;
		std::unordered_set<ClassDefinition *> seenClassDefinitions;
		std::vector<Section *> touchedSections;
		std::unordered_set<Section *> seenSections;
		std::vector<SectionInstantiationUndo> sectionInstantiationUndo;
		std::unordered_set<std::string> seenSectionInstantiations;
		std::vector<InstantiationUndo> instantiationUndo;
		std::unordered_set<Instantiation *> seenInstantiations;

		static std::string sectionInstantiationUndoId(Section *section, const InstantiationKey &key) {
			std::string keyString = std::to_string(reinterpret_cast<uintptr_t>(section)) + "|";
			for (const DataType &type : key.argumentTypes)
				keyString += encodeDataTypeForCacheKey(type) + ";";
			keyString += "|";
			for (const auto &[name, value] : key.compileTimeParameters) {
				keyString += name + "=";
				keyString += encodeCompileTimeValueForCacheKey(value);
				keyString += ";";
			}
			return keyString;
		}

		void recordVariableWrite(Variable *var) {
			if (!var || seenVariables.contains(var))
				return;
			seenVariables.insert(var);
			variableTypeUndo.push_back({var, var->type, var->typeOriginRange, var->typeOriginFloatLiteralReplacement});
		}

		void recordClassInstantiationAppend(ClassDefinition *classDef) {
			if (!classDef || seenClassDefinitions.contains(classDef))
				return;
			seenClassDefinitions.insert(classDef);
			classInstantiationSizes.push_back({classDef, classDef->instantiations.size()});
		}

		void recordTouchedSection(Section *section) {
			if (!section || seenSections.contains(section))
				return;
			seenSections.insert(section);
			touchedSections.push_back(section);
		}

		void recordSectionInstantiationWrite(Section *section, const InstantiationKey &key) {
			if (!section)
				return;
			std::string keyString = sectionInstantiationUndoId(section, key);
			if (seenSectionInstantiations.contains(keyString))
				return;
			seenSectionInstantiations.insert(keyString);
			auto it = section->instantiations.find(key);
			if (it == section->instantiations.end())
				sectionInstantiationUndo.push_back({section, key, false, {}});
			else
				sectionInstantiationUndo.push_back({section, key, true, it->second});
		}

		void recordInstantiationWrite(Instantiation *instantiation) {
			if (!instantiation || seenInstantiations.contains(instantiation))
				return;
			seenInstantiations.insert(instantiation);
			instantiationUndo.push_back({instantiation, std::make_unique<Instantiation>(*instantiation)});
		}

		void absorb(TrialJournal &&nested) {
			for (VariableUndo &undo : nested.variableTypeUndo) {
				if (seenVariables.insert(undo.variable).second)
					variableTypeUndo.push_back(std::move(undo));
			}
			for (auto &entry : nested.classInstantiationSizes) {
				if (seenClassDefinitions.insert(entry.first).second)
					classInstantiationSizes.push_back(std::move(entry));
			}
			for (Section *section : nested.touchedSections) {
				if (seenSections.insert(section).second)
					touchedSections.push_back(section);
			}
			for (SectionInstantiationUndo &undo : nested.sectionInstantiationUndo) {
				std::string id = sectionInstantiationUndoId(undo.section, undo.key);
				if (seenSectionInstantiations.insert(std::move(id)).second)
					sectionInstantiationUndo.push_back(std::move(undo));
			}
			for (InstantiationUndo &undo : nested.instantiationUndo) {
				if (seenInstantiations.insert(undo.instantiation).second)
					instantiationUndo.push_back(std::move(undo));
			}
		}

		SectionInstantiationRetargetResult
		retargetSectionInstantiationWrite(Section *section, const InstantiationKey &fromKey, const InstantiationKey &toKey) {
			if (!section)
				return SectionInstantiationRetargetResult::MissingSourceRecord;
			if (fromKey == toKey)
				return SectionInstantiationRetargetResult::Updated;
			std::string fromId = sectionInstantiationUndoId(section, fromKey);
			auto fromSeenIt = seenSectionInstantiations.find(fromId);
			if (fromSeenIt == seenSectionInstantiations.end())
				return SectionInstantiationRetargetResult::MissingSourceRecord;
			auto undoIt = std::find_if(
				sectionInstantiationUndo.begin(), sectionInstantiationUndo.end(),
				[&](const SectionInstantiationUndo &undo) {
				return undo.section == section && undo.key == fromKey;
			}
			);
			if (undoIt == sectionInstantiationUndo.end())
				return SectionInstantiationRetargetResult::MissingSourceRecord;
			if (undoIt->existed)
				return SectionInstantiationRetargetResult::SourceWasPreexisting;
			std::string toId = sectionInstantiationUndoId(section, toKey);
			if (seenSectionInstantiations.contains(toId))
				return SectionInstantiationRetargetResult::TargetAlreadyRecorded;
			seenSectionInstantiations.erase(fromSeenIt);
			seenSectionInstantiations.insert(std::move(toId));
			undoIt->key = toKey;
			return SectionInstantiationRetargetResult::Updated;
		}
	};

	struct GroupingTrialJournal {
		using ExpressionValueMap = std::unordered_map<Expression *, CompileTimeValue>;
		using CodeLineGroupingMap = std::unordered_map<CodeLine *, TrialCodeLineGrouping>;
		using CallableInstantiationMap = std::unordered_map<PatternDefinition *, Instantiation *>;
		template <typename Map, typename Key> using SeenWrites = std::unordered_map<Map *, std::unordered_set<Key *>>;

		struct ExpressionValueUndo {
			ExpressionValueMap *map;
			Expression *expression;
			bool existed;
			CompileTimeValue value;
		};

		struct CodeLineGroupingUndo {
			CodeLineGroupingMap *map;
			CodeLine *line;
			bool existed;
			TrialCodeLineGrouping grouping;
		};

		struct CallableInstantiationUndo {
			CallableInstantiationMap *map;
			PatternDefinition *definition;
			bool existed;
			Instantiation *instantiation;
		};

		std::vector<ExpressionValueUndo> expressionValueUndo;
		SeenWrites<ExpressionValueMap, Expression> seenExpressionValueWrites;
		std::vector<CodeLineGroupingUndo> codeLineGroupingUndo;
		SeenWrites<CodeLineGroupingMap, CodeLine> seenCodeLineGroupingWrites;
		std::vector<CallableInstantiationUndo> callableInstantiationUndo;
		SeenWrites<CallableInstantiationMap, PatternDefinition> seenCallableInstantiationWrites;

		template <typename Map, typename Key>
		static bool recordFirstWrite(SeenWrites<Map, Key> &seenWrites, Map *map, Key *key) {
			return seenWrites[map].insert(key).second;
		}

		void recordExpressionValueWrite(ExpressionValueMap &map, Expression *expression) {
			if (!recordFirstWrite(seenExpressionValueWrites, &map, expression))
				return;
			auto existing = map.find(expression);
			expressionValueUndo.push_back(
				{&map, expression, existing != map.end(), existing != map.end() ? existing->second : CompileTimeValue{}}
			);
		}

		void recordCodeLineGroupingWrite(CodeLineGroupingMap &map, CodeLine *line) {
			if (!recordFirstWrite(seenCodeLineGroupingWrites, &map, line))
				return;
			auto existing = map.find(line);
			codeLineGroupingUndo.push_back(
				{&map, line, existing != map.end(), existing != map.end() ? existing->second : TrialCodeLineGrouping{}}
			);
		}

		void recordCallableInstantiationWrite(CallableInstantiationMap &map, PatternDefinition *definition) {
			if (!recordFirstWrite(seenCallableInstantiationWrites, &map, definition))
				return;
			auto existing = map.find(definition);
			callableInstantiationUndo.push_back(
				{&map, definition, existing != map.end(), existing != map.end() ? existing->second : nullptr}
			);
		}

		void absorb(GroupingTrialJournal &&nested) {
			for (ExpressionValueUndo &undo : nested.expressionValueUndo) {
				if (recordFirstWrite(seenExpressionValueWrites, undo.map, undo.expression))
					expressionValueUndo.push_back(std::move(undo));
			}
			for (CodeLineGroupingUndo &undo : nested.codeLineGroupingUndo) {
				if (recordFirstWrite(seenCodeLineGroupingWrites, undo.map, undo.line))
					codeLineGroupingUndo.push_back(std::move(undo));
			}
			for (CallableInstantiationUndo &undo : nested.callableInstantiationUndo) {
				if (recordFirstWrite(seenCallableInstantiationWrites, undo.map, undo.definition))
					callableInstantiationUndo.push_back(std::move(undo));
			}
		}

		void rollback() {
			for (auto undo = callableInstantiationUndo.rbegin(); undo != callableInstantiationUndo.rend(); ++undo) {
				if (undo->existed)
					(*undo->map)[undo->definition] = undo->instantiation;
				else
					undo->map->erase(undo->definition);
			}
			for (auto undo = codeLineGroupingUndo.rbegin(); undo != codeLineGroupingUndo.rend(); ++undo) {
				if (undo->existed)
					(*undo->map)[undo->line] = std::move(undo->grouping);
				else
					undo->map->erase(undo->line);
			}
			for (auto undo = expressionValueUndo.rbegin(); undo != expressionValueUndo.rend(); ++undo) {
				if (undo->existed)
					(*undo->map)[undo->expression] = std::move(undo->value);
				else
					undo->map->erase(undo->expression);
			}
		}
	};

	struct RecursiveInferenceObservationFrame {
		Instantiation *owner{};
		RecursiveInferenceObservationFrame *parent{};
		bool observed = false;
	};

	ParseContext &parseContext;
	Instantiation *currentInstantiation{};
	RecursiveInferenceObservationFrame *recursiveInferenceObservationFrame{};
	InstantiatedSectionBody *currentInstantiatedSectionBody{};
	std::vector<SectionFlexBodyInferenceFrame> sectionFlexBodyFrames;
	std::vector<Section *> activeFlexDefinitionStack;
	std::vector<std::optional<FlexExpansionKey>> activeFlexExpansionKeys;
	std::vector<Expression *> activeFlexCallStack;
	std::vector<Section *> flexCallSiteSectionStack;
	// Flow-sensitive variable values. A monostate entry explicitly records that
	// a previously evaluated variable is unknown in the current execution state.
	KnownConstantState currentVariableValues;
	// Flow-sensitive provenance for pointer variables whose runtime value may
	// address local or global variables tracked by constant inference.
	AddressInferenceState currentAddressState;
	SubjectState currentSubject;
	bool typesValid = true;
	bool trial = false;
	bool suppressDiagnostics = false;
	bool suppressReinferPassDiagnostics = false;
	std::string typeFailureDetail;
	std::vector<RelatedInfo> typeFailureRelatedInfo;
	DiagnosticExpressionSnapshot typeFailureSnapshot;
	Diagnostic typeFailureDiagnostic;
	int typeFailurePriority = -1;
	bool hasTypeFailureDiagnostic = false;
	TrialJournal *trialJournal{};
	GroupingTrialJournal *groupingTrialJournal{};
	// Signature inference sets this signal while probing a type-constraint
	// expression. Encountering an overload whose own signature is unresolved
	// defers the probe instead of choosing a declaration-order candidate.
	std::shared_ptr<bool> unresolvedPatternConstraintSignal;
	const std::unordered_set<Expression *> *fixedGroupingRoots{};
	std::unordered_set<Expression *> *resolvedGroupingRoots{};
	bool detectGroupingAmbiguity = false;
	std::vector<OperandGroupingWarning> *pendingOperandGroupingWarnings{};
	std::vector<Expression *> expressionStack;
	std::unordered_map<Expression *, CompileTimeValue> trialExpressionValues;
	const std::unordered_map<Expression *, CompileTimeValue> *inheritedTrialExpressionValues{};
	std::unordered_map<CodeLine *, TrialCodeLineGrouping> trialCodeLineGroupings;
	std::unordered_map<PatternDefinition *, Instantiation *> trialCallableInstantiations;

	InferenceContext(ParseContext &pc) : parseContext(pc) {}
	InferenceContext(ParseContext &pc, bool trial) : parseContext(pc), trial(trial) {}

	void inheritSectionExecutionState(const InferenceContext &other) {
		currentInstantiatedSectionBody = other.currentInstantiatedSectionBody;
		sectionFlexBodyFrames = other.sectionFlexBodyFrames;
		activeFlexDefinitionStack = other.activeFlexDefinitionStack;
		activeFlexExpansionKeys = other.activeFlexExpansionKeys;
		activeFlexCallStack = other.activeFlexCallStack;
		flexCallSiteSectionStack = other.flexCallSiteSectionStack;
	}

	void addDiagnostic(Diagnostic diagnostic) {
		if (!trial && !suppressDiagnostics && !suppressReinferPassDiagnostics)
			parseContext.addDiagnostic(std::move(diagnostic));
	}

	Expression *currentExpression() const { return expressionStack.empty() ? nullptr : expressionStack.back(); }

	void pushExpression(Expression *expression) { expressionStack.push_back(expression); }

	void popExpression() {
		requireCompilerInvariant(!expressionStack.empty(), "Expression stack underflow during type inference");
		expressionStack.pop_back();
	}

	std::vector<RelatedInfo> captureInferenceTraceRelatedInfo(Expression *currentExpressionOverride = nullptr) const {
		std::vector<RelatedInfo> relatedInfo;
		std::unordered_set<Expression *> seenExpressions;
		if (currentExpressionOverride && expressionParticipatesInInferenceTrace(currentExpressionOverride) &&
			currentExpressionOverride->range.line) {
			relatedInfo.push_back(
				{describeInferenceTraceFrame(currentExpressionOverride, parseContext), currentExpressionOverride->range}
			);
			seenExpressions.insert(currentExpressionOverride);
		}
		for (auto it = expressionStack.rbegin(); it != expressionStack.rend(); ++it) {
			Expression *expression = *it;
			if (!expressionParticipatesInInferenceTrace(expression) || !expression->range.line)
				continue;
			if (!seenExpressions.insert(expression).second)
				continue;
			relatedInfo.push_back({describeInferenceTraceFrame(expression, parseContext), expression->range});
		}
		return relatedInfo;
	}

	void appendCurrentInferenceTrace(Diagnostic &diagnostic) const {
		std::vector<RelatedInfo> trace = captureInferenceTraceRelatedInfo();
		for (RelatedInfo &frame : trace) {
			bool alreadyPresent = std::ranges::any_of(diagnostic.relatedInfo, [&](const RelatedInfo &existing) {
				return existing.message == frame.message && existing.range.line == frame.range.line &&
					   existing.range.start() == frame.range.start() && existing.range.end() == frame.range.end();
			});
			if (!alreadyPresent)
				diagnostic.relatedInfo.push_back(std::move(frame));
		}
	}

	void insertTypeFailureCause(std::vector<RelatedInfo> causes) {
		if (causes.empty())
			return;
		requireCompilerInvariant(
			!typeFailureRelatedInfo.empty(), "a typed failure cause requires an originating inference frame"
		);
		typeFailureRelatedInfo.insert(typeFailureRelatedInfo.begin() + 1, causes.begin(), causes.end());
	}

	void addDiagnosticWithCurrentTrace(Diagnostic diagnostic) {
		appendCurrentInferenceTrace(diagnostic);
		addDiagnostic(std::move(diagnostic));
	}

	void setTypeFailure(std::string detail) {
		typesValid = false;
		if (typeFailureDetail.empty()) {
			typeFailureDetail = std::move(detail);
			typeFailureRelatedInfo = captureInferenceTraceRelatedInfo();
			typeFailureSnapshot = captureDiagnosticExpressionSnapshot(currentExpression());
		}
	}

	void fail(Diagnostic diagnostic, int priority = 1, bool includeInferenceTrace = true) {
		typesValid = false;
		if (hasTypeFailureDiagnostic && typeFailurePriority >= priority)
			return;
		if (includeInferenceTrace)
			appendCurrentInferenceTrace(diagnostic);
		typeFailureDiagnostic = std::move(diagnostic);
		typeFailurePriority = priority;
		hasTypeFailureDiagnostic = true;
	}

	void clearTypeFailure() {
		typeFailureDetail.clear();
		typeFailureRelatedInfo.clear();
		typeFailureSnapshot = {};
		typeFailureDiagnostic = Diagnostic();
		typeFailurePriority = -1;
		hasTypeFailureDiagnostic = false;
	}

	void inheritTypeFailureFrom(const InferenceContext &other) {
		if (!hasTypeFailureDiagnostic && other.hasTypeFailureDiagnostic) {
			typeFailureDiagnostic = other.typeFailureDiagnostic;
			typeFailurePriority = other.typeFailurePriority;
			hasTypeFailureDiagnostic = true;
		}
		if (!typeFailureDetail.empty() || other.typeFailureDetail.empty())
			return;
		typeFailureDetail = other.typeFailureDetail;
		typeFailureRelatedInfo = other.typeFailureRelatedInfo;
		typeFailureSnapshot = other.typeFailureSnapshot;
	}

	VariableReference *normalizeReference(VariableReference *reference) const {
		if (!reference)
			return nullptr;
		return reference->definition ? reference->definition : reference;
	}

	CompileTimeValue lookupExpressionValue(Expression *expression) const {
		if (!expression)
			return {};
		if (expression->kind == Expression::Kind::Variable && expression->variable) {
			VariableReference *key = normalizeReference(expression->variable);
			const KnownConstantStorage &knownConstants = currentVariableValues.read();
			auto currentValue = knownConstants.find(key);
			if (currentValue != knownConstants.end())
				return currentValue->second;
		}
		if (trial) {
			auto trialIt = trialExpressionValues.find(expression);
			if (trialIt != trialExpressionValues.end())
				return trialIt->second;
			if (inheritedTrialExpressionValues) {
				auto inheritedIt = inheritedTrialExpressionValues->find(expression);
				if (inheritedIt != inheritedTrialExpressionValues->end())
					return inheritedIt->second;
			}
		}
		return getExpressionCompileTimeValue(expression);
	}

	void setExpressionValue(Expression *expression, const CompileTimeValue &value) {
		if (!expression)
			return;
		if (trial) {
			if (groupingTrialJournal)
				groupingTrialJournal->recordExpressionValueWrite(trialExpressionValues, expression);
			if (isCompileTimeKnown(value))
				trialExpressionValues[expression] = value;
			else
				trialExpressionValues.erase(expression);
			return;
		}
		setExpressionCompileTimeValue(expression, value);
	}

	Expression *lookupFlexExpansion(Expression *expression) const {
		return expression ? expression->inferredFlexExpansion : nullptr;
	}

	CompileTimeValue lookupKnownConstant(VariableReference *reference) const {
		VariableReference *key = normalizeReference(reference);
		if (!key)
			return {};
		const KnownConstantStorage &knownConstants = currentVariableValues.read();
		auto it = knownConstants.find(key);
		return it != knownConstants.end() ? it->second : CompileTimeValue{};
	}

	void setKnownConstant(VariableReference *reference, const CompileTimeValue &value) {
		VariableReference *key = normalizeReference(reference);
		if (!key)
			return;
		currentVariableValues.write()[key] = value;
	}

	AddressProvenance lookupAddressProvenance(VariableReference *reference) const {
		VariableReference *key = normalizeReference(reference);
		if (!key)
			return {.mayTargets = {}, .unknown = true};
		const VariableAddressProvenance &variables = currentAddressState.read().variables;
		auto it = variables.find(key);
		return it != variables.end() ? it->second : AddressProvenance{.mayTargets = {}, .unknown = true};
	}

	void setAddressProvenance(VariableReference *reference, AddressProvenance provenance) {
		VariableReference *key = normalizeReference(reference);
		if (!key)
			return;
		currentAddressState.write().variables[key] = std::move(provenance);
	}

	void noteWrittenGlobalReference(VariableReference *reference) {
		if (!currentInstantiation || !reference)
			return;
		VariableReference *key = normalizeReference(reference);
		if (key) {
			if (trial) {
				requireCompilerInvariant(trialJournal, "trial global-write mutation requires a rollback journal");
				trialJournal->recordInstantiationWrite(currentInstantiation);
			}
			currentInstantiation->writtenGlobalReferences.insert(key);
		}
	}
};
