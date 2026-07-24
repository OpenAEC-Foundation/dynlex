bool validatePatternDefinitionConflicts(ParseContext &context) { return emitDefinitionConflicts(context); }

void addVariableReferencesFromMatch(ParseContext &context, PatternReference *reference, PatternMatch &match) {
	int offset = reference->range().start();
	for (VariableMatch &varMatch : match.discoveredVariables) {
		VariableReference *varRef = context.createVariableReference(
			Range(reference->range().line, offset + varMatch.lineStartPos, offset + varMatch.lineEndPos), varMatch.name
		);
		varMatch.variableReference = varRef;
		reference->range().section()->addVariableReference(context, varRef);
	}
	for (PatternMatch &subMatch : match.subMatches) {
		addVariableReferencesFromMatch(context, reference, subMatch);
	}
}

static void expandMatch(Expression *rootExpression, Expression *expr, PatternMatch *match);

static VariableReference *findVisibleVariableReference(Section *section, const std::string &name) {
	for (Section *current = section; current; current = current->parent) {
		auto it = current->variableReferences.find(name);
		if (it != current->variableReferences.end() && !it->second.empty())
			return it->second.front();
	}
	return nullptr;
}

static Expression *
materializeMatchedArgument(Expression *rootExpression, PatternMatch *match, const MatchedArgument &argument) {
	switch (argument.kind) {
	case MatchedArgument::Kind::Expression: {
		Expression *capturedExpression = argument.expression;
		if (capturedExpression && rootExpression && rootExpression->range.line && rootExpression->range.line->section)
			expandExpression(capturedExpression, rootExpression->range.line->section);
		return capturedExpression;
	}
	case MatchedArgument::Kind::SubMatch: {
		PatternMatch &subMatch = match->subMatches[argument.itemIndex];
		Expression *arg = new Expression();
		arg->isSubMatch = true;
		arg->range = Range(
			rootExpression->range.line, rootExpression->range.start() + subMatch.lineStartPos,
			rootExpression->range.start() + subMatch.lineEndPos
		);
		expandMatch(rootExpression, arg, &subMatch);
		return arg;
	}
	case MatchedArgument::Kind::Variable: {
		const VariableMatch &varMatch = match->discoveredVariables[argument.itemIndex];
		Expression *arg = new Expression();
		arg->kind = Expression::Kind::Variable;
		arg->variable = varMatch.variableReference;
		arg->range = varMatch.variableReference->range;
		return arg;
	}
	case MatchedArgument::Kind::Word: {
		const WordMatch &wordMatch = match->discoveredWords[argument.itemIndex];
		Expression *arg = new Expression();
		arg->kind = Expression::Kind::Literal;
		arg->literalValue = wordMatch.text;
		arg->range = Range(
			rootExpression->range.line, rootExpression->range.start() + wordMatch.lineStartPos,
			rootExpression->range.start() + wordMatch.lineEndPos
		);
		return arg;
	}
	}
	return nullptr;
}

static void expandMatch(Expression *rootExpression, Expression *expr, PatternMatch *match) {
	expr->kind = Expression::Kind::PatternCall;
	expr->patternMatch = match;
	expr->selectedPatternDefinition = nullptr;
	expr->selectedPatternPathIndex = std::nullopt;
	std::vector<MatchedArgument> orderedArguments = match->orderedArguments;
	std::stable_sort(
		orderedArguments.begin(), orderedArguments.end(),
		[](const MatchedArgument &left, const MatchedArgument &right) {
		return left.argumentIndex < right.argumentIndex;
	}
	);
	expr->arguments.clear();
	expr->arguments.reserve(orderedArguments.size());
	for (const MatchedArgument &argument : orderedArguments) {
		if (Expression *materialized = materializeMatchedArgument(rootExpression, match, argument))
			expr->arguments.push_back(materialized);
	}
}

// Recursively expand pending expressions to their resolved forms
void expandExpression(Expression *expr, Section *section) {
	// Expand children first
	for (Expression *arg : expr->arguments) {
		expandExpression(arg, section);
	}

	// If this is a pending expression, resolve it
	// we can assume that expr->patternReference is set, since pending expressions are only expanded after complete pattern
	// resolution
	if (expr->kind == Expression::Kind::Pending) {
		PatternReference *ref = expr->patternReference;
		if (ref->match) {
			expandMatch(expr, expr, ref->match);
		} else if (ref->patternElements.size() == 1 && ref->patternElements[0].type == PatternElement::Type::Variable) {
			// Resolved to a variable reference
			expr->kind = Expression::Kind::Variable;
			std::string varName = ref->patternElements[0].text;
			expr->variable = findVisibleVariableReference(section, varName);
		} else if (expr->arguments.size() == 1 && expr->arguments[0]->kind == Expression::Kind::IntrinsicCall) {
			// If the pattern is just an argument placeholder and we have a single intrinsic call,
			// promote the intrinsic to be this expression
			Expression *intrinsic = expr->arguments[0];
			expr->kind = intrinsic->kind;
			expr->intrinsicName = intrinsic->intrinsicName;
			expr->arguments = intrinsic->arguments;
			expr->range = intrinsic->range;
		}
	}
}

// Collect all body references from a definition section's descendants (including through nested definitions,
// since nested code can access parent parameters).
static void collectBodyReferences(Section *section, std::vector<PatternReference *> &refs) {
	for (Section *child : section->children) {
		refs.insert(refs.end(), child->patternReferences.begin(), child->patternReferences.end());
		collectBodyReferences(child, refs);
	}
}

// Compute initial variableLikeCounts for each definition section.
static void computeVariableLikeCounts(std::list<Section *> &sections) {
	for (Section *section : sections) {
		std::vector<PatternReference *> bodyRefs;
		collectBodyReferences(section, bodyRefs);

		// Collect all VL texts from all definitions in this section
		std::unordered_set<std::string> vlTexts;
		for (PatternDefinition *def : section->patternDefinitions) {
			forEachLeafElement(def->patternElements, [&](PatternElement &elem) {
				if (elem.type == PatternElement::Type::VariableLike)
					vlTexts.insert(elem.text);
			});
		}

		// Count body references that contain each VL text
		for (const std::string &vlText : vlTexts) {
			int count = 0;
			for (PatternReference *ref : bodyRefs) {
				for (const PatternElement &refElem : ref->patternElements) {
					if (refElem.type == PatternElement::Type::VariableLike && refElem.text == vlText) {
						count++;
						break;
					}
				}
			}
			section->variableLikeCounts[vlText] = count;
		}
	}
}

// After a body reference resolves, decrement VL counts on all ancestor definition sections
// (nested code can access parent parameters).
static void decrementVariableLikeCounts(PatternReference *reference) {
	Section *sec = reference->range().section();
	while (sec) {
		if (!sec->patternDefinitions.empty()) {
			for (const PatternElement &refElem : reference->patternElements) {
				if (refElem.type == PatternElement::Type::VariableLike) {
					auto it = sec->variableLikeCounts.find(refElem.text);
					if (it != sec->variableLikeCounts.end() && it->second > 0)
						it->second--;
				}
			}
		}
		sec = sec->parent;
	}
}

// Recursively record which definition each reference matched (including sub-match definitions)
static void trackMatchDefinitions(
	PatternMatch &match, PatternReference *reference,
	std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> &defToRefs
) {
	if (!match.matchingDefinitions.empty()) {
		for (auto *def : match.matchingDefinitions) {
			auto &refs = defToRefs[def];
			if (std::find(refs.begin(), refs.end(), reference) == refs.end())
				refs.push_back(reference);
		}
	}
	for (PatternMatch &subMatch : match.subMatches) {
		trackMatchDefinitions(subMatch, reference, defToRefs);
	}
}

// Recursively remove a reference from all definition tracking buckets represented in this match tree.
static void untrackMatchDefinitions(
	PatternMatch &match, PatternReference *reference,
	std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> &defToRefs
) {
	if (!match.matchingDefinitions.empty()) {
		for (auto *def : match.matchingDefinitions) {
			auto it = defToRefs.find(def);
			if (it == defToRefs.end())
				continue;
			auto &vec = it->second;
			vec.erase(std::remove(vec.begin(), vec.end(), reference), vec.end());
		}
	}
	for (PatternMatch &subMatch : match.subMatches) {
		untrackMatchDefinitions(subMatch, reference, defToRefs);
	}
}

// Inverse of decrementVariableLikeCounts — re-increment VL counts for a reference being un-resolved
static void incrementVariableLikeCounts(PatternReference *reference) {
	Section *sec = reference->range().section();
	while (sec) {
		if (!sec->patternDefinitions.empty()) {
			for (const PatternElement &refElem : reference->patternElements) {
				if (refElem.type == PatternElement::Type::VariableLike) {
					auto it = sec->variableLikeCounts.find(refElem.text);
					if (it != sec->variableLikeCounts.end())
						it->second++;
				}
			}
		}
		sec = sec->parent;
	}
}

static Range firstMatchedDefinitionRange(PatternMatch *match) { return match->matchingDefinitions.front()->range; }

static void emitExplicitDefinitionParameterAmbiguityWarnings(ParseContext &context) {
	std::function<void(Section *)> visitSection = [&](Section *section) {
		for (PatternDefinition *definition : section->patternDefinitions) {
			forEachLeafElement(definition->patternElements, [&](DefinitionPatternElement &element) {
				if (element.type != PatternElement::Type::Variable || element.typeConstraintName.empty())
					return;

				PatternDefinition *singleWordFunction = findDefinitionBySignature(
					context, SectionType::Function, element.text,
					definition->range.line ? definition->range.line->sourceFile : nullptr
				);
				if (!singleWordFunction)
					return;

				Range parameterRange = definition->range;
				if (definition->range.line) {
					int start = definition->range.start() + static_cast<int>(element.startPos);
					parameterRange = Range(definition->range.line, start, start + static_cast<int>(element.text.size()));
				}

				const SyntaxConfig &syntax = syntaxConfigForRange(context, parameterRange);
				Diagnostic warning(
					context, Diagnostic::Level::Warning, "explicit parameter ambiguity", parameterRange, "parameter",
					element.text
				);
				warning.relatedInfo.push_back(
					{renderConfiguredMessage(syntax, "explicit parameter ambiguity related parameter"), parameterRange}
				);
				if (singleWordFunction->range.line) {
					warning.relatedInfo.push_back(
						{renderConfiguredMessage(syntax, "explicit parameter ambiguity related function"),
						 singleWordFunction->range}
					);
				}
				context.diagnostics.push_back(std::move(warning));
			});
		}
		for (Section *child : section->children)
			visitSection(child);
	};

	visitSection(context.mainSection);
}

static void emitDuplicatePatternWordWarnings(ParseContext &context) {
	std::list<PatternReference *> bodyReferences;
	std::list<PatternReference *> globalReferences;
	std::list<Section *> sections;
	context.mainSection->collectPatternReferencesAndSections(bodyReferences, globalReferences, sections);

	for (Section *section : sections) {
		if (isInternalSection(section))
			continue;

		for (PatternDefinition *definition : section->patternDefinitions) {
			using FoundRanges = std::unordered_map<std::string, Range>;
			std::function<FoundRanges(std::vector<DefinitionPatternElement> &, const FoundRanges &)> visit =
				[&](std::vector<DefinitionPatternElement> &elements, const FoundRanges &incomingFound) {
				FoundRanges found = incomingFound;
				for (DefinitionPatternElement &element : elements) {
					if (element.type == PatternElement::Type::Choice) {
						FoundRanges foundAfterChoice = found;
						for (auto &alternative : element.alternatives) {
							FoundRanges alternativeFound = visit(alternative, found);
							for (const auto &[name, range] : alternativeFound)
								foundAfterChoice.try_emplace(name, range);
						}
						found = std::move(foundAfterChoice);
						continue;
					}
					if (element.type == PatternElement::Type::Variable) {
						found.try_emplace(element.text, definitionElementRange(definition, element));
						continue;
					}
					if (element.type != PatternElement::Type::VariableLike)
						continue;

					auto foundIt = found.find(element.text);
					if (foundIt == found.end())
						continue;

					Diagnostic diagnostic;
					diagnostic.level = Diagnostic::Level::Warning;
					diagnostic.range = definitionElementRange(definition, element);
					diagnostic.message =
						"Repeated pattern word '" + element.text + "' stays literal because it already became a parameter";
					diagnostic.relatedInfo.push_back({"The first parameter occurrence was here:", foundIt->second});
					context.diagnostics.push_back(std::move(diagnostic));
				}
				return found;
			};
			visit(definition->patternElements, {});
		}
	}
}

// Remove VariableReferences created from a match, undoing addVariableReferencesFromMatch and searchParentPatterns effects.
// If any ancestor definitions had VL→Variable promotions reverted, their sections are added to affectedSections.
