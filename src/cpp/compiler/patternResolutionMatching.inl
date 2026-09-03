static void removeVariableReferencesFromMatch(
	ParseContext &context, PatternReference *reference, PatternMatch &match, std::vector<Section *> &affectedSections,
	bool revertImplicitPromotions
) {
	Section *refSection = reference->range().section();
	for (VariableMatch &varMatch : match.discoveredVariables) {
		const std::string &name = varMatch.variableReference->name;

		// Remove from section's variableReferences
		auto it = refSection->variableReferences.find(name);
		if (it != refSection->variableReferences.end()) {
			auto &vec = it->second;
			vec.erase(std::remove(vec.begin(), vec.end(), varMatch.variableReference), vec.end());
			if (vec.empty())
				refSection->variableReferences.erase(it);
		}

		// Remove from unresolvedVariableReferences
		auto uit = context.unresolvedVariableReferences.find(name);
		if (uit != context.unresolvedVariableReferences.end()) {
			auto &vec = uit->second;
			vec.erase(std::remove(vec.begin(), vec.end(), varMatch.variableReference), vec.end());
			if (vec.empty())
				context.unresolvedVariableReferences.erase(uit);
		}

		// Undo searchParentPatterns effects when requested. Stale invalidation may call
		// this with revertImplicitPromotions=false to avoid transient promotion thrash
		// while references are being immediately re-matched in the same pass.
		if (varMatch.variableReference->definition) {
			VariableReference *definitionReference = varMatch.variableReference->definition;
			Section *ownerSection = findDefinitionOwnerSection(refSection, name, definitionReference);
			if (ownerSection)
				appendUniqueSection(affectedSections, ownerSection);
			if (revertImplicitPromotions && ownerSection &&
				!sectionSubtreeHasBoundReferenceToDefinition(ownerSection, name, definitionReference)) {
				// Remove variableDefinitions entry if it was created by searchParentPatterns.
				auto defIt = ownerSection->variableDefinitions.find(name);
				if (defIt != ownerSection->variableDefinitions.end() && defIt->second == definitionReference) {
					// Remove the definition VarRef from variableReferences too.
					auto vit = ownerSection->variableReferences.find(name);
					if (vit != ownerSection->variableReferences.end()) {
						auto &vec = vit->second;
						vec.erase(std::remove(vec.begin(), vec.end(), definitionReference), vec.end());
						if (vec.empty())
							ownerSection->variableReferences.erase(vit);
					}
					ownerSection->variableDefinitions.erase(defIt);
					eraseOwnedSectionVariable(ownerSection, name, definitionReference);
				}

				// Revert Variable→VariableLike through the indexed-definition transaction.
				for (PatternDefinition *def : ownerSection->patternDefinitions) {
					std::vector<DefinitionPatternElement *> elementsToRevert;
					forEachLeafElement(def->patternElements, [&](DefinitionPatternElement &element) {
						// Only revert unconstrained Variables — typed arguments ({type:name})
						// were never VariableLike and must not be reverted.
						if (element.type == PatternElement::Type::Variable && element.text == name &&
							element.typeConstraintName.empty() && element.promotedFromVariableLike)
							elementsToRevert.push_back(&element);
					});
					for (DefinitionPatternElement *element : elementsToRevert)
						revertImplicitPatternParameter(context, *def, *element);
					if (!elementsToRevert.empty())
						appendUniqueSection(affectedSections, ownerSection);
				}
			}
		}

		varMatch.variableReference = nullptr;
	}
	for (PatternMatch &subMatch : match.subMatches)
		removeVariableReferencesFromMatch(context, reference, subMatch, affectedSections, revertImplicitPromotions);
}

// Un-resolve a reference: undo all effects and prepare it for re-matching.
// Returns the set of definition sections that had their VL classification affected.
static std::vector<Section *> unresolveReference(
	ParseContext &context, PatternReference *reference,
	std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> &defToRefs, bool revertImplicitPromotions = true
) {
	std::vector<Section *> affectedSections;
	// Remove variable references created from the match
	removeVariableReferencesFromMatch(context, reference, *reference->match, affectedSections, revertImplicitPromotions);

	// Re-increment VL counts (inverse of decrementVariableLikeCounts done during resolve)
	incrementVariableLikeCounts(reference);

	// Remove from defToRefs tracking (including submatches)
	untrackMatchDefinitions(*reference->match, reference, defToRefs);

	// Clear match and mark unresolved
	delete reference->match;
	reference->match = nullptr;
	reference->resolved = false;
	reference->range().section()->incrementUnresolved();
	traceResolution("unresolve " + referenceTraceId(reference));

	return affectedSections;
}

static bool cleanupStaleImplicitPromotionsInSection(ParseContext &context, Section *section) {
	bool changed = false;
	std::vector<std::pair<std::string, VariableReference *>> ownedDefinitions;
	ownedDefinitions.reserve(section->variableDefinitions.size());
	for (const auto &[name, definitionReference] : section->variableDefinitions)
		ownedDefinitions.emplace_back(name, definitionReference);

	for (const auto &[name, definitionReference] : ownedDefinitions) {
		if (sectionSubtreeHasBoundReferenceToDefinition(section, name, definitionReference))
			continue;

		auto defIt = section->variableDefinitions.find(name);
		if (defIt != section->variableDefinitions.end() && defIt->second == definitionReference) {
			auto vit = section->variableReferences.find(name);
			if (vit != section->variableReferences.end()) {
				auto &vec = vit->second;
				vec.erase(std::remove(vec.begin(), vec.end(), definitionReference), vec.end());
				if (vec.empty())
					section->variableReferences.erase(vit);
			}
			section->variableDefinitions.erase(defIt);
			eraseOwnedSectionVariable(section, name, definitionReference);
		}

		for (PatternDefinition *def : section->patternDefinitions) {
			std::vector<DefinitionPatternElement *> elementsToRevert;
			forEachLeafElement(def->patternElements, [&](DefinitionPatternElement &element) {
				if (element.type == PatternElement::Type::Variable && element.text == name &&
					element.typeConstraintName.empty() && element.promotedFromVariableLike)
					elementsToRevert.push_back(&element);
			});
			for (DefinitionPatternElement *element : elementsToRevert)
				revertImplicitPatternParameter(context, *def, *element);
			changed = changed || !elementsToRevert.empty();
		}
	}

	return changed;
}

static bool patternMatchUsesDefinition(const PatternMatch &match, const PatternDefinition *definition) {
	if (std::find(match.matchingDefinitions.begin(), match.matchingDefinitions.end(), definition) !=
		match.matchingDefinitions.end())
		return true;
	return std::any_of(match.subMatches.begin(), match.subMatches.end(), [&](const PatternMatch &subMatch) {
		return patternMatchUsesDefinition(subMatch, definition);
	});
}

using FailedMatchDependencies = std::unordered_map<PatternReference *, MatchDependencies>;

static SectionType definitionPatternTreeType(const Section *section) {
	requireCompilerInvariant(section != nullptr, "pattern definition has no owning section");
	if (section->isConversion)
		return SectionType::Conversion;
	if (section->type == SectionType::Class)
		return SectionType::Function;
	return section->type;
}

static bool prepareConversionPattern(ParseContext &context, PatternDefinition &definition) {
	Section *section = definition.section;
	if (!section || !section->isConversion)
		return true;
	if (definition.patternElements.size() == 1) {
		DefinitionPatternElement &element = definition.patternElements.front();
		if (element.type == PatternElement::Type::VariableLike && !element.isExplicitLiteral) {
			element.type = PatternElement::Type::Variable;
			return true;
		}
		if (element.type == PatternElement::Type::Variable)
			return true;
	}
	context.diagnostics.push_back(
		Diagnostic(context, Diagnostic::Level::Error, "conversion requires one parameter", definition.range)
	);
	return false;
}

static bool matchDependenciesChanged(const MatchDependencies &dependencies) {
	return std::ranges::any_of(dependencies, [](const MatchDependency &dependency) {
		switch (dependency.kind) {
		case MatchDependency::Kind::Endpoint:
			return dependency.node->endpointRevision != dependency.endpointRevision;
		case MatchDependency::Kind::ArgumentChild:
			return dependency.node->argumentChild != nullptr;
		case MatchDependency::Kind::WordChild:
			return dependency.node->wordChild != nullptr;
		case MatchDependency::Kind::LiteralChild:
			return dependency.node->literalChildren.contains(dependency.literal);
		}
		crashCompilerBug("unknown failed match dependency kind");
	});
}

static bool isArgumentExpressionReference(const PatternReference *reference) {
	requireCompilerInvariant(
		reference && reference->expression && reference->range().line,
		"pattern reference argument classification requires a source expression"
	);
	return reference->range().line->expression != reference->expression;
}

// Resolve a list of pattern references against the tree. Returns true if all resolved.
static bool resolveReferences(
	ParseContext &context, std::list<PatternReference *> &references, bool decrementCounts, bool allowUnmatchedVariables,
	FailedMatchDependencies &failedMatchDependencies,
	std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> *defToRefs = nullptr, const char *phase = "body",
	PatternReference **activeReference = nullptr, PatternMatch **activeMatch = nullptr,
	bool *activeReferenceNeedsRematch = nullptr, bool *deferredActiveRematch = nullptr,
	std::vector<Section *> *pendingPromotionCleanupSections = nullptr
) {
	return std::erase_if(references, [&](PatternReference *reference) {
		PatternMatch *match = nullptr;
		MatchDependencies dependencies;
		auto failedMatch = failedMatchDependencies.find(reference);
		bool shouldRetry = failedMatch == failedMatchDependencies.end() || matchDependenciesChanged(failedMatch->second);
		if (shouldRetry) {
			if (failedMatch != failedMatchDependencies.end())
				failedMatchDependencies.erase(failedMatch);
			match = context.match(reference, {}, &dependencies);
		}
		if (match) {
			if (activeReference)
				*activeReference = reference;
			if (activeMatch)
				*activeMatch = match;
			if (activeReferenceNeedsRematch)
				*activeReferenceNeedsRematch = false;
			if (reference->patternElements.size() == 1 &&
				reference->patternElements[0].type == PatternElement::Type::VariableLike) {
				const std::string &token = reference->patternElements[0].text;
				if (findEnclosingParameterCandidate(reference, token)) {
					AlternativePatternSuggestion suggestion = findAlternativePatternSuggestion(reference, match, token);
					const SyntaxConfig &syntax = syntaxConfigForRange(context, reference->range());
					std::string suggestionMessage =
						suggestion.spelling.empty()
							? renderConfiguredMessage(syntax, "ambiguous single word reference", "suggestion fallback")
							: " Consider using '" + suggestion.spelling + "'.";
					Diagnostic diag(
						context, Diagnostic::Level::Warning, "ambiguous single word reference", reference->range(), "token",
						token, "suggestion", suggestionMessage
					);
					for (const Range &candidateRange : collectEnclosingParameterCandidateRanges(reference, token))
						diag.relatedInfo.push_back(
							{renderConfiguredMessage(syntax, "ambiguous single word reference", "related parameter"),
							 candidateRange}
						);
					Range functionRange = firstMatchedDefinitionRange(match);
					if (functionRange.line)
						diag.relatedInfo.push_back(
							{renderConfiguredMessage(syntax, "ambiguous single word reference", "related matched pattern"),
							 functionRange}
						);
					if (suggestion.definition && suggestion.definition->range.line)
						diag.relatedInfo.push_back(
							{renderConfiguredMessage(syntax, "ambiguous single word reference", "related suggestion"),
							 suggestion.definition->range}
						);
					if (suggestion.isMultiWord && !suggestion.spelling.empty()) {
						diag.quickFixes.push_back({
							renderConfiguredMessage(
								syntax, "ambiguous single word reference", "quick fix", {{"spelling", suggestion.spelling}}
							),
							reference->range(),
							suggestion.spelling,
						});
					}
					// Intentionally no per-reference dedupe tracking here.
					context.diagnostics.push_back(std::move(diag));
				}
			}

			// Multi-word reference matched a pattern in the tree
			reference->resolve(match);
			addVariableReferencesFromMatch(context, reference, *match);
			if (decrementCounts)
				decrementVariableLikeCounts(reference);
			if (defToRefs)
				trackMatchDefinitions(*match, reference, *defToRefs);
			traceResolution(std::string(phase) + " resolved " + referenceTraceId(reference));
			if (activeReferenceNeedsRematch && *activeReferenceNeedsRematch) {
				requireCompilerInvariant(
					defToRefs && deferredActiveRematch && pendingPromotionCleanupSections,
					"active pattern rematch requires resolution bookkeeping"
				);
				*deferredActiveRematch = true;
				*activeReferenceNeedsRematch = false;
				if (activeReference)
					*activeReference = nullptr;
				if (activeMatch)
					*activeMatch = nullptr;
				auto affectedSections = unresolveReference(context, reference, *defToRefs, false);
				for (Section *affectedSection : affectedSections)
					appendUniqueSection(*pendingPromotionCleanupSections, affectedSection);
				return false;
			}
			if (activeReference)
				*activeReference = nullptr;
			if (activeMatch)
				*activeMatch = nullptr;
		} else if (reference->patternElements.size() == 1 &&
				   reference->patternElements[0].type == PatternElement::Type::VariableLike) {
			const std::string &varName = reference->patternElements[0].text;
			if (!isArgumentExpressionReference(reference) ||
				(!allowUnmatchedVariables && !findEnclosingParameterCandidate(reference, varName)))
				return false;
			failedMatchDependencies.erase(reference);
			// An unmatched word can only be a variable when another expression consumes it as an argument.
			reference->patternElements[0].type = PatternElement::Type::Variable;
			reference->resolve();
			reference->range().section()->addVariableReference(
				context, context.createVariableReference(reference->range(), reference->patternElements[0].text)
			);
			if (decrementCounts)
				decrementVariableLikeCounts(reference);
			traceResolution(std::string(phase) + " resolved-as-variable " + referenceTraceId(reference));
		} else if (shouldRetry) {
			failedMatchDependencies.emplace(reference, std::move(dependencies));
		}
		return reference->resolved;
	}) > 0;
}

// step 3: loop over code, resolve patterns and build up a pattern tree until all patterns are resolved
bool resolvePatterns(ParseContext &context) {
	generateClassPropertyPatterns(context);
	std::list<PatternReference *> bodyReferences;
	std::list<PatternReference *> globalReferences;
	std::list<Section *> unResolvedSections;
	context.mainSection->collectPatternReferencesAndSections(bodyReferences, globalReferences, unResolvedSections);
	traceResolution(
		"start sections=" + std::to_string(unResolvedSections.size()) + " body_refs=" + std::to_string(bodyReferences.size()) +
		" global_refs=" + std::to_string(globalReferences.size())
	);
	bool hadPatternParseError = false;
	for (Section *unResolvedSection : unResolvedSections) {
		for (PatternDefinition *unresolvedDefinition : unResolvedSection->patternDefinitions) {
			if (unresolvedDefinition->hasPrebuiltPatternElements) {
				unResolvedSection->indexExplicitParameters(*unresolvedDefinition);
				continue;
			}
			std::vector<DefinitionPatternElement> parsedElements;
			if (!parsePatternElements(
					context, unresolvedDefinition->range, unresolvedDefinition->range.subString, parsedElements
				)) {
				hadPatternParseError = true;
				continue;
			}
			unresolvedDefinition->patternElements = std::move(parsedElements);
			if (!prepareConversionPattern(context, *unresolvedDefinition)) {
				hadPatternParseError = true;
				continue;
			}
			unResolvedSection->indexExplicitParameters(*unresolvedDefinition);
		}
	}
	if (hadPatternParseError)
		return false;
	for (Section *section : unResolvedSections) {
		if (section->type == SectionType::Class)
			populateClassPatternNames(section);
	}
	for (PatternReference *ref : bodyReferences)
		ref->patternElements = getPatternElements(ref->pattern.text);
	for (PatternReference *ref : globalReferences)
		ref->patternElements = getPatternElements(ref->pattern.text);

	// Compute initial VL counts before resolution
	computeVariableLikeCounts(unResolvedSections);

	// add the roots
	std::generate(std::begin(context.patternTrees), std::end(context.patternTrees), []() {
		return new PatternTreeNode(PatternElement::Type::Other, "");
	});

	// Phase 1: resolve body references and definitions
	std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> definitionToReferences;
	FailedMatchDependencies failedBodyMatchDependencies;
	bool staleInvalidationOccurred = false;
	std::vector<Section *> pendingPromotionCleanupSections;
	std::vector<PatternReference *> referencesToRequeue;
	PatternReference *activeResolvingReference = nullptr;
	PatternMatch *activeResolvingMatch = nullptr;
	bool activeReferenceNeedsRematch = false;

	auto queueReferenceForRematch = [&](PatternReference *reference) {
		if (!reference || !reference->resolved)
			return;
		staleInvalidationOccurred = true;
		if (reference == activeResolvingReference) {
			activeReferenceNeedsRematch = true;
			return;
		}
		auto affectedSections = unresolveReference(context, reference, definitionToReferences, false);
		for (Section *affectedSection : affectedSections)
			appendUniqueSection(pendingPromotionCleanupSections, affectedSection);
		if (std::find(referencesToRequeue.begin(), referencesToRequeue.end(), reference) == referencesToRequeue.end())
			referencesToRequeue.push_back(reference);
	};

	auto appendRequeuedReferences = [&]() {
		bool appended = false;
		std::sort(referencesToRequeue.begin(), referencesToRequeue.end(), referenceComesBefore);
		for (PatternReference *reference : referencesToRequeue) {
			if (reference->resolved ||
				std::find(bodyReferences.begin(), bodyReferences.end(), reference) != bodyReferences.end())
				continue;
			bodyReferences.push_back(reference);
			appended = true;
		}
		referencesToRequeue.clear();
		return appended;
	};

	// Helper: after adding a definition to the tree, find less-specific definitions
	// and unresolve any references that matched them so they can re-match the more specific one.
	auto invalidateStaleMatches = [&](PatternDefinition *definition, SectionType treeType) {
		auto lessSpecific = context.patternTrees[(size_t)treeType]->findLessSpecificDefinitions(definition->patternElements);
		for (PatternTreeNode *endNode : definition->endNodes) {
			for (PatternDefinition *candidate : endNode->matchingDefinitions) {
				if (candidate != definition &&
					std::find(lessSpecific.begin(), lessSpecific.end(), candidate) == lessSpecific.end())
					lessSpecific.push_back(candidate);
			}
		}
		std::sort(lessSpecific.begin(), lessSpecific.end(), patternDefinitionComesBefore);
		traceResolution(
			"invalidate base=" + definitionTraceId(definition) + " candidates=" + std::to_string(lessSpecific.size())
		);
		for (PatternDefinition *lessDef : lessSpecific) {
			if (activeResolvingReference && activeResolvingMatch &&
				patternMatchUsesDefinition(*activeResolvingMatch, lessDef)) {
				const lsp::SourceFile *sourceFile =
					activeResolvingReference->range().line ? activeResolvingReference->range().line->sourceFile : nullptr;
				requireCompilerInvariant(sourceFile != nullptr, "active stale match invalidation requires a source file");
				if (isPatternDefinitionVisibleFromSource(*definition, *sourceFile))
					queueReferenceForRematch(activeResolvingReference);
			}
			auto it = definitionToReferences.find(lessDef);
			if (it == definitionToReferences.end())
				continue;
			// Copy the vector since unresolveReference modifies it
			std::vector<PatternReference *> refs = it->second;
			std::sort(refs.begin(), refs.end(), referenceComesBefore);
			for (PatternReference *ref : refs) {
				if (!ref->resolved)
					continue;
				const lsp::SourceFile *sourceFile = ref->range().line ? ref->range().line->sourceFile : nullptr;
				requireCompilerInvariant(sourceFile != nullptr, "stale match invalidation requires a source file");
				if (!isPatternDefinitionVisibleFromSource(*definition, *sourceFile))
					continue;
				queueReferenceForRematch(ref);
			}
		}
	};

	// Helper: add a definition to the pattern tree.
	auto addDefinitionToTree = [&](PatternDefinition *definition, SectionType treeType) {
		context.patternTrees[(size_t)treeType]->addPatternDefinition(definition, treeType);
		traceResolution("add " + definitionTraceId(definition) + " tree=" + std::to_string((int)treeType));
		invalidateStaleMatches(definition, treeType);
	};

	context.indexedPatternDefinitionMutation = [&](PatternDefinition &definition, const std::function<void()> &mutation) {
		requireCompilerInvariant(definition.resolved, "indexed pattern mutation requires a resolved definition");
		PatternTreeNode *tree = definition.indexedTree;
		SectionType treeType = definition.indexedTreeType;
		requireCompilerInvariant(
			tree != nullptr && tree == context.patternTrees[(size_t)treeType],
			"indexed pattern mutation records inconsistent tree identity"
		);

		auto referencesIt = definitionToReferences.find(&definition);
		if (referencesIt != definitionToReferences.end()) {
			std::vector<PatternReference *> oldReferences = referencesIt->second;
			std::sort(oldReferences.begin(), oldReferences.end(), referenceComesBefore);
			for (PatternReference *reference : oldReferences)
				queueReferenceForRematch(reference);
		}
		if (activeResolvingReference && activeResolvingMatch && patternMatchUsesDefinition(*activeResolvingMatch, &definition))
			queueReferenceForRematch(activeResolvingReference);

		tree->removePatternDefinition(&definition);
		mutation();
		tree->addPatternDefinition(&definition, treeType);
		traceResolution("reindex " + definitionTraceId(&definition) + " tree=" + std::to_string((int)treeType));
		invalidateStaleMatches(&definition, treeType);
		staleInvalidationOccurred = true;
	};
	struct IndexedPatternMutationScope {
		ParseContext &context;
		~IndexedPatternMutationScope() { context.indexedPatternDefinitionMutation = {}; }
	} indexedPatternMutationScope{context};

	for (int resolutionIteration = 0; resolutionIteration < context.options.maxResolutionIterations; resolutionIteration++) {

		bool madeProgress = false;
		staleInvalidationOccurred = false;
		size_t sectionsBefore = unResolvedSections.size();
		traceResolution(
			"iter " + std::to_string(resolutionIteration) + " begin sections=" + std::to_string(unResolvedSections.size()) +
			" body_refs=" + std::to_string(bodyReferences.size())
		);
		if (resolutionTraceEnabled()) {
			std::vector<Section *> debugSections(unResolvedSections.begin(), unResolvedSections.end());
			std::sort(debugSections.begin(), debugSections.end(), sectionComesBefore);
			std::ostringstream sectionList;
			sectionList << "iter " << resolutionIteration << " section-list";
			for (Section *section : debugSections)
				sectionList << " | " << sectionTraceId(section);
			traceResolution(sectionList.str());
		}

		// each iteration, we go over all sections first
		std::erase_if(unResolvedSections, [&](Section *section) {
			section->patternDefinitionsResolved = true;
			for (PatternDefinition *definition : section->patternDefinitions) {
				if (!definition->resolved) {
					definition->resolved = true;
					bool resolveImmediately = hasSingleWordPatternSpelling(definition->patternElements);
					forEachLeafElement(definition->patternElements, [&](DefinitionPatternElement &element) {
						if (!resolveImmediately && section->canPromoteImplicitParameter(*definition, element)) {
							if (definition->patternElements.size() > 1) {
								if (section->variableLikeCounts[element.text] != 0) {
									definition->resolved = false;
									section->patternDefinitionsResolved = false;
								}
							}
						}
					});
					if (definition->resolved) {
						SectionType treeType = definitionPatternTreeType(section);
						addDefinitionToTree(definition, treeType);
					}
				}
			}
			if (!section->patternDefinitionsResolved) {
				section->patternDefinitionsResolved = section->unresolvedCount == 0;
			}
			if (section->patternDefinitionsResolved) {
				for (PatternDefinition *definition : section->patternDefinitions) {
					if (!definition->resolved) {
						definition->resolved = true;
						SectionType treeType = definitionPatternTreeType(section);
						addDefinitionToTree(definition, treeType);
					}
				}
			}
			return section->patternDefinitionsResolved;
		});
		if (unResolvedSections.size() < sectionsBefore) {
			madeProgress = true;
		}
		if (staleInvalidationOccurred) {
			madeProgress = true;
		}

		appendRequeuedReferences();
		activeResolvingReference = nullptr;
		activeResolvingMatch = nullptr;
		activeReferenceNeedsRematch = false;
		bool deferredActiveRematch = false;
		bool resolvedReference = resolveReferences(
			context, bodyReferences, true, unResolvedSections.empty(), failedBodyMatchDependencies, &definitionToReferences,
			"body", &activeResolvingReference, &activeResolvingMatch, &activeReferenceNeedsRematch, &deferredActiveRematch,
			&pendingPromotionCleanupSections
		);
		activeResolvingReference = nullptr;
		activeResolvingMatch = nullptr;
		activeReferenceNeedsRematch = false;
		bool appendedAfterResolution = appendRequeuedReferences();
		if (resolvedReference || deferredActiveRematch || appendedAfterResolution || staleInvalidationOccurred) {
			madeProgress = true;
		}
		if (!deferredActiveRematch && !appendedAfterResolution && !pendingPromotionCleanupSections.empty()) {
			std::vector<Section *> sectionsToClean = std::move(pendingPromotionCleanupSections);
			pendingPromotionCleanupSections.clear();
			std::sort(sectionsToClean.begin(), sectionsToClean.end(), sectionComesBefore);
			bool cleanupChanged = false;
			for (Section *sec : sectionsToClean) {
				if (!cleanupStaleImplicitPromotionsInSection(context, sec))
					continue;
				cleanupChanged = true;
			}
			appendRequeuedReferences();
			if (cleanupChanged)
				madeProgress = true;
		}

		if (unResolvedSections.empty() && bodyReferences.empty())
			break;

		// Deadlock detection: if no progress was made, we have a cycle (self-recursion,
		// mutual recursion, or any dependency loop). Break the cycle by force-resolving
		// all remaining sections — their unresolved VL elements become parameters.
		// The cyclic body references will then resolve against the newly-added definitions
		// in the next iteration.
		if (!madeProgress) {
			bool promotedCyclicParameter = false;
			for (Section *section : unResolvedSections) {
				std::unordered_set<std::string> promotableNames;
				std::unordered_set<Section *> visitedSections;
				collectPromotablePatternNames(section, promotableNames, visitedSections);
				if (promotableNames.empty())
					continue;
				for (PatternReference *reference : bodyReferences) {
					Section *referenceSection = reference->range().section();
					if (!sectionContainsOrIsAncestorOf(section, referenceSection))
						continue;
					// Deadlock recovery must not reinterpret multi-word call literals as
					// parameters; only unresolved single-token references are valid
					// evidence for implicit parameter promotion here.
					if (reference->patternElements.size() != 1)
						continue;
					const PatternElement &element = reference->patternElements.front();
					if (element.type != PatternElement::Type::VariableLike || !promotableNames.contains(element.text))
						continue;
					if (promotePatternNameInSectionChain(context, referenceSection, element.text, reference->range())) {
						promotedCyclicParameter = true;
						traceResolution(
							"iter " + std::to_string(resolutionIteration) + " cyclic promote '" + element.text + "' from " +
							referenceTraceId(reference)
						);
					}
				}
			}
			if (promotedCyclicParameter) {
				computeVariableLikeCounts(unResolvedSections);
				continue;
			}
			traceResolution("iter " + std::to_string(resolutionIteration) + " deadlock force-resolve");
			if (unResolvedSections.empty())
				break; // no sections to force-resolve and no progress — truly stuck
			// Force-resolve all remaining sections by adding their definitions as-is.
			// Copy the list first because addDefinitionToTree may trigger invalidations
			// whose cleanup re-adds sections on the next iteration.
			std::list<Section *> toForceResolve = std::move(unResolvedSections);
			unResolvedSections.clear();
			for (Section *section : toForceResolve) {
				section->patternDefinitionsResolved = true;
				for (PatternDefinition *definition : section->patternDefinitions) {
					if (!definition->resolved) {
						definition->resolved = true;
						SectionType treeType = definitionPatternTreeType(section);
						addDefinitionToTree(definition, treeType);
					}
				}
			}
			// Invalidation cleanup runs during the next iteration.
		}
	}

	auto emitUnresolvedPatternDiagnostics = [&]() {
		traceResolution(
			"failed sections=" + std::to_string(unResolvedSections.size()) +
			" body_refs=" + std::to_string(bodyReferences.size()) + " global_refs=" + std::to_string(globalReferences.size())
		);
		for (Section *sec : unResolvedSections) {
			for (PatternDefinition *def : sec->patternDefinitions) {
				context.diagnostics.push_back(Diagnostic(
					context, Diagnostic::Level::Error, "unresolved pattern definition", def->range, "pattern",
					(std::string)def->range.subString
				));
			}
		}
		auto emitReferenceDiagnostic = [&](PatternReference *reference) {
			Diagnostic diagnostic =
				reference->purpose == PatternReference::Purpose::TypeConstraint
					? unknownTypeConstraintDiagnostic(context, reference->range(), reference->range().subString)
					: Diagnostic(context, Diagnostic::Level::Error, "unresolved pattern", reference->range());
			appendUnusedLiteralParameterNotes(context, reference, diagnostic);
			context.diagnostics.push_back(std::move(diagnostic));
		};
		for (PatternReference *reference : bodyReferences) {
			emitReferenceDiagnostic(reference);
		}
		for (PatternReference *reference : globalReferences) {
			emitReferenceDiagnostic(reference);
		}
	};

	// Commit phase 1 only after definitions and definition bodies are fully resolved.
	if (!unResolvedSections.empty() || !bodyReferences.empty()) {
		emitUnresolvedPatternDiagnostics();
		return false;
	}
	context.compilationStage = ParseContext::CompilationStage::ResolvedFunctionPatterns;
	// Phase 2: resolve global references (all definitions are now in the tree).
	// Typed-domain conflicts are checked after signature inference, when every
	// constraint has a concrete type.
	emitDuplicatePatternWordWarnings(context);

	FailedMatchDependencies failedGlobalMatchDependencies;
	for (int resolutionIteration = 0; resolutionIteration < context.options.maxResolutionIterations; resolutionIteration++) {
		resolveReferences(context, globalReferences, false, true, failedGlobalMatchDependencies, nullptr, "global");
		if (globalReferences.empty())
			break;
	}

	if (!globalReferences.empty()) {
		emitUnresolvedPatternDiagnostics();
		return false;
	}
	context.compilationStage = ParseContext::CompilationStage::ResolvedGlobalPatterns;
	emitExplicitDefinitionParameterAmbiguityWarnings(context);

	// Phase 3: Resolve precedence declarations and re-match affected references
	{
		auto resolveSignature = [&](const std::string &signature, PatternDefinition *from) {
			const lsp::SourceFile *sourceFile = from->range.line ? from->range.line->sourceFile : nullptr;
			return findDefinitionsBySignature(context, SectionType::Function, signature, sourceFile);
		};

		struct PrecedenceEdge {
			PatternDefinition *higher;
			PatternDefinition *lower;
		};
		std::vector<PrecedenceEdge> edges;
		std::unordered_set<PatternDefinition *> involvedDefs;

		// "default" is a virtual node between patterns declared before and after it.
		PatternDefinition defaultSentinel(Range(), nullptr);

		std::function<bool(Section *)> collectPrecedence = [&](Section *section) -> bool {
			if (!section->beforePatterns.empty() || !section->afterPatterns.empty()) {
				for (PatternDefinition *def : section->patternDefinitions) {
					involvedDefs.insert(def);

					// before: B means "this definition evaluates before B" = higher precedence
					for (const std::string &beforeStr : section->beforePatterns) {
						if (beforeStr == "default") {
							involvedDefs.insert(&defaultSentinel);
							edges.push_back({def, &defaultSentinel});
							continue;
						}
						std::vector<PatternDefinition *> targets = resolveSignature(beforeStr, def);
						if (targets.empty()) {
							context.diagnostics.push_back(Diagnostic(
								context, Diagnostic::Level::Error, "precedence target not found", def->range, "target",
								beforeStr
							));
							return false;
						}
						for (PatternDefinition *target : targets) {
							involvedDefs.insert(target);
							edges.push_back({def, target});
						}
					}

					// after: A means "this definition evaluates after A" = lower precedence
					for (const std::string &afterStr : section->afterPatterns) {
						if (afterStr == "default") {
							involvedDefs.insert(&defaultSentinel);
							edges.push_back({&defaultSentinel, def});
							continue;
						}
						std::vector<PatternDefinition *> targets = resolveSignature(afterStr, def);
						if (targets.empty()) {
							context.diagnostics.push_back(Diagnostic(
								context, Diagnostic::Level::Error, "precedence target not found", def->range, "target", afterStr
							));
							return false;
						}
						for (PatternDefinition *target : targets) {
							involvedDefs.insert(target);
							edges.push_back({target, def});
						}
					}
				}
			}
			for (Section *child : section->children)
				if (!collectPrecedence(child))
					return false;
			return true;
		};

		if (!collectPrecedence(context.mainSection))
			return false;

		std::vector<PatternDefinition *> precedenceTargets(involvedDefs.begin(), involvedDefs.end());
		std::function<void(Section *)> placeGeneratedPropertyAccessors = [&](Section *section) {
			for (PatternDefinition *definition : section->patternDefinitions) {
				if (!definition->isGeneratedClassPropertyAccessor)
					continue;
				involvedDefs.insert(definition);
				for (PatternDefinition *target : precedenceTargets)
					edges.push_back({definition, target});
			}
			for (Section *child : section->children)
				placeGeneratedPropertyAccessors(child);
		};
		placeGeneratedPropertyAccessors(context.mainSection);

		auto patternFamily = [&](PatternDefinition *definition) {
			return definition == &defaultSentinel ? std::vector<PatternDefinition *>{definition}
												  : connectedPatternFamily(definition);
		};
		std::set<std::pair<PatternDefinition *, PatternDefinition *>> familyEdges;
		for (const PrecedenceEdge &edge : edges) {
			for (PatternDefinition *higher : patternFamily(edge.higher)) {
				for (PatternDefinition *lower : patternFamily(edge.lower)) {
					familyEdges.insert({higher, lower});
					involvedDefs.insert(higher);
					involvedDefs.insert(lower);
				}
			}
		}
		edges.clear();
		edges.reserve(familyEdges.size());
		for (const auto &[higher, lower] : familyEdges)
			edges.push_back({higher, lower});

		if (!involvedDefs.empty()) {
			// Topological sort (Kahn's algorithm) validates the expanded syntax-family graph.
			std::unordered_map<PatternDefinition *, std::vector<PatternDefinition *>> adjList;
			std::unordered_map<PatternDefinition *, int> inDegree;
			for (PatternDefinition *def : involvedDefs)
				inDegree[def] = 0;
			for (auto &edge : edges) {
				adjList[edge.higher].push_back(edge.lower);
				inDegree[edge.lower]++;
			}

			std::vector<PatternDefinition *> zeroInDegree;
			zeroInDegree.reserve(inDegree.size());
			for (auto &[def, deg] : inDegree) {
				if (deg == 0) {
					zeroInDegree.push_back(def);
				}
			}
			std::sort(zeroInDegree.begin(), zeroInDegree.end(), patternDefinitionComesBefore);

			// Validate the authored partial order without collapsing unrelated
			// definitions into numeric tiers.
			size_t processedCount = 0;
			std::vector<PatternDefinition *> currentWave = zeroInDegree;
			while (!currentWave.empty()) {
				std::vector<PatternDefinition *> nextWave;
				for (PatternDefinition *def : currentWave) {
					processedCount++;
					for (PatternDefinition *lower : adjList[def]) {
						if (--inDegree[lower] == 0)
							nextWave.push_back(lower);
					}
				}
				std::sort(nextWave.begin(), nextWave.end(), patternDefinitionComesBefore);
				nextWave.erase(std::unique(nextWave.begin(), nextWave.end()), nextWave.end());
				currentWave = std::move(nextWave);
			}

			if (processedCount != involvedDefs.size()) {
				context.diagnostics.push_back(
					Diagnostic(context, Diagnostic::Level::Error, "precedence cycle detected", Range())
				);
				return false;
			}

			for (PatternDefinition *definition : involvedDefs) {
				if (definition != &defaultSentinel)
					definition->precedenceSuccessors.clear();
			}
			for (PatternDefinition *definition : involvedDefs) {
				if (definition == &defaultSentinel)
					continue;
				std::vector<PatternDefinition *> pending(adjList[definition].begin(), adjList[definition].end());
				std::unordered_set<PatternDefinition *> visited;
				while (!pending.empty()) {
					PatternDefinition *successor = pending.back();
					pending.pop_back();
					if (!visited.insert(successor).second)
						continue;
					if (successor != &defaultSentinel)
						definition->precedenceSuccessors.insert(successor);
					auto adjacent = adjList.find(successor);
					if (adjacent != adjList.end())
						pending.insert(pending.end(), adjacent->second.begin(), adjacent->second.end());
				}
			}

			// Precedence re-matching is NOT done here — operand reordering in type inference
			// handles grouping selection using the resolved partial order.
		}
	}
	context.compilationStage = ParseContext::CompilationStage::ResolvedPatternPrecedence;
	// All patterns resolved — expand expressions and resolve variable references
	for (CodeLine *line : context.codeLines) {
		if (line->expression)
			expandExpression(line->expression, line->section);
	}
	std::vector<std::string> unresolvedNames;
	unresolvedNames.reserve(context.unresolvedVariableReferences.size());
	for (auto &[name, _] : context.unresolvedVariableReferences)
		unresolvedNames.push_back(name);
	std::sort(unresolvedNames.begin(), unresolvedNames.end());

	for (const std::string &name : unresolvedNames) {
		auto refsIt = context.unresolvedVariableReferences.find(name);
		if (refsIt == context.unresolvedVariableReferences.end())
			continue;
		auto &references = refsIt->second;
		bool isGlobal = context.declaredGlobalVariables.contains(name);

		std::unordered_map<Section *, Section *> sectionToHighest;
		for (VariableReference *ref : references) {
			Section *sec = ref->range.section();
			if (sectionToHighest.contains(sec))
				continue;
			Section *highest = sec;

			// Find the enclosing function (Function/Effect section)
			Section *functionScope = nullptr;
			for (Section *a = sec; a; a = a->parent) {
				if (a->type == SectionType::Function && !a->isFlex) {
					functionScope = a;
					break;
				}
			}

			// Check if THIS function declares the variable as global
			bool declaredGlobalHere = false;
			if (functionScope) {
				for (const std::string &globalVar : functionScope->globalVariables) {
					if (globalVar == name) {
						declaredGlobalHere = true;
						break;
					}
				}
			}

			for (Section *a = sec->parent; a; a = a->parent) {
				// Stop at function boundary unless this function declares it as global
				if (!declaredGlobalHere && a == functionScope) {
					break;
				}
				if (a->variableReferences.contains(name))
					highest = a;
			}
			sectionToHighest[sec] = highest;
		}
		std::unordered_map<Section *, std::vector<VariableReference *>> groups;
		for (VariableReference *ref : references)
			groups[sectionToHighest[ref->range.section()]].push_back(ref);
		std::vector<Section *> orderedGroupSections;
		orderedGroupSections.reserve(groups.size());
		for (auto &[highestSection, _] : groups)
			orderedGroupSections.push_back(highestSection);
		std::sort(orderedGroupSections.begin(), orderedGroupSections.end(), sectionComesBefore);

		for (Section *highestSection : orderedGroupSections) {
			auto groupIt = groups.find(highestSection);
			if (groupIt == groups.end())
				continue;
			auto &groupRefs = groupIt->second;
			VariableReference *definition = *std::min_element(groupRefs.begin(), groupRefs.end(), [](auto *a, auto *b) {
				return a->range.line->mergedLineIndex < b->range.line->mergedLineIndex;
			});

			// This group is the global one if the variable is declared global and
			// the group's highest section is at the top level (no enclosing function)
			bool groupIsGlobal = false;
			if (isGlobal) {
				groupIsGlobal = true;
				for (Section *a = highestSection; a; a = a->parent) {
					if (a->type == SectionType::Function && !a->isFlex) {
						groupIsGlobal = false;
						break;
					}
				}
			}

			auto existingDefinition = highestSection->variableDefinitions.find(name);
			if (existingDefinition != highestSection->variableDefinitions.end() && existingDefinition->second != definition) {
				VariableReference *oldDefinition = existingDefinition->second;
				auto refsIt = highestSection->variableReferences.find(name);
				if (refsIt != highestSection->variableReferences.end()) {
					auto &refs = refsIt->second;
					refs.erase(std::remove(refs.begin(), refs.end(), oldDefinition), refs.end());
					if (refs.empty())
						highestSection->variableReferences.erase(refsIt);
				}
			}
			highestSection->variableDefinitions[name] = definition;
			auto variableIt = highestSection->variables.find(name);
			if (variableIt == highestSection->variables.end()) {
				highestSection->variables.emplace(name, new Variable(name, definition, groupIsGlobal));
			} else {
				requireCompilerInvariant(variableIt->second, "section variable map contains a null variable");
				*variableIt->second = Variable(name, definition, groupIsGlobal);
			}
			for (VariableReference *ref : groupRefs) {
				if (ref != definition)
					ref->definition = definition;
			}
		}
	}
	return true;
}
