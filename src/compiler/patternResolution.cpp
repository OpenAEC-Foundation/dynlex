#include "classSection.h"
#include "compiler.h"
#include "expression.h"
#include "patternElement.h"
#include "patternTreeNode.h"
#include "transformedPattern.h"
#include "type.h"
#include "variable.h"
#include <algorithm>
#include <list>
#include <queue>
#include <ranges>
#include <unordered_set>

void addVariableReferencesFromMatch(ParseContext &context, PatternReference *reference, PatternMatch &match) {
	int offset = reference->range().start();
	for (VariableMatch &varMatch : match.discoveredVariables) {
		VariableReference *varRef = new VariableReference(
			Range(reference->range().line, offset + varMatch.lineStartPos, offset + varMatch.lineEndPos), varMatch.name
		);
		varMatch.variableReference = varRef;
		reference->range().section()->addVariableReference(context, varRef);
	}
	for (PatternMatch &subMatch : match.subMatches) {
		addVariableReferencesFromMatch(context, reference, subMatch);
	}
}

void expandMatch(Expression *rootExpression, Expression *expr, PatternMatch *match) {
	expr->arguments = match->arguments;
	// move arguments to the appropriate submatches
	expr->kind = Expression::Kind::PatternCall;
	expr->patternMatch = match;
	for (const PatternMatch &subMatch : match->subMatches) {
		Expression *arg = new Expression();
		arg->range = Range(
			expr->range.line, rootExpression->range.start() + subMatch.lineStartPos,
			rootExpression->range.start() + subMatch.lineEndPos
		);
		expandMatch(rootExpression, arg, const_cast<PatternMatch *>(&subMatch));
		expr->arguments.push_back(arg);
	}

	// Handle discoveredVariables - add Variable expressions using stored references
	for (const VariableMatch &varMatch : match->discoveredVariables) {
		Expression *arg = new Expression();
		arg->kind = Expression::Kind::Variable;
		arg->variable = varMatch.variableReference;
		arg->range = varMatch.variableReference->range;
		expr->arguments.push_back(arg);
	}

	// Handle discoveredWords - add string Literal expressions
	for (const WordMatch &wordMatch : match->discoveredWords) {
		Expression *arg = new Expression();
		arg->kind = Expression::Kind::Literal;
		arg->literalValue = wordMatch.text;
		arg->range = Range(
			rootExpression->range.line, rootExpression->range.start() + wordMatch.lineStartPos,
			rootExpression->range.start() + wordMatch.lineEndPos
		);
		expr->arguments.push_back(arg);
	}
}

// Recursively expand pending expressions to their resolved forms
void expandExpression(Expression *expr, Section *section) {
	if (!expr)
		return;

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
			// Find the variable reference in the section
			std::string varName = ref->patternElements[0].text;
			auto it = section->variableReferences.find(varName);
			if (it != section->variableReferences.end() && !it->second.empty()) {
				expr->variable = it->second.front();
			}
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
	if (match.matchedEndNode && match.matchedEndNode->matchingDefinition) {
		defToRefs[match.matchedEndNode->matchingDefinition].push_back(reference);
	}
	for (PatternMatch &subMatch : match.subMatches) {
		trackMatchDefinitions(subMatch, reference, defToRefs);
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

// Remove VariableReferences created from a match, undoing addVariableReferencesFromMatch and searchParentPatterns effects.
// If any ancestor definitions had VL→Variable promotions reverted, their sections are added to affectedSections.
static void removeVariableReferencesFromMatch(
	ParseContext &context, PatternReference *reference, PatternMatch &match, std::unordered_set<Section *> &affectedSections
) {
	Section *refSection = reference->range().section();
	for (VariableMatch &varMatch : match.discoveredVariables) {
		if (!varMatch.variableReference)
			continue;
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

		// Undo searchParentPatterns effects: if this varRef caused a VL→Variable promotion
		// in an ancestor definition, and no other references to this name remain in the section,
		// revert the element back to VariableLike
		if (varMatch.variableReference->definition) {
			// Check if there are still other references to this name in the section
			bool hasOtherRefs = refSection->variableReferences.contains(name);
			if (!hasOtherRefs) {
				// Walk up parent sections to find the definition that was modified
				for (Section *sec = refSection; sec; sec = sec->parent) {
					// Remove variableDefinitions entry if it was created by searchParentPatterns
					auto defIt = sec->variableDefinitions.find(name);
					if (defIt != sec->variableDefinitions.end()) {
						// Remove the definition VarRef from variableReferences too
						auto vit = sec->variableReferences.find(name);
						if (vit != sec->variableReferences.end()) {
							auto &vec = vit->second;
							vec.erase(std::remove(vec.begin(), vec.end(), defIt->second), vec.end());
							if (vec.empty())
								sec->variableReferences.erase(vit);
						}
						sec->variableDefinitions.erase(defIt);
					}

					// Revert Variable→VariableLike in pattern definitions and mark for re-resolution.
					// Must remove from tree BEFORE changing element types (tree was built with old types).
					for (PatternDefinition *def : sec->patternDefinitions) {
						bool needsReResolution = false;
						forEachLeafElement(def->patternElements, [&](PatternElement &element) {
							if (element.type == PatternElement::Type::Variable && element.text == name)
								needsReResolution = true;
						});
						if (needsReResolution && def->resolved) {
							// Remove from tree while elements still reflect the old path
							SectionType treeType = sec->type == SectionType::Class ? SectionType::Expression : sec->type;
							context.patternTrees[(size_t)treeType]->removePatternPart(def->patternElements, def);
						}
						// Now revert element types
						forEachLeafElement(def->patternElements, [&](PatternElement &element) {
							if (element.type == PatternElement::Type::Variable && element.text == name) {
								element.type = PatternElement::Type::VariableLike;
								def->resolved = false;
								sec->patternDefinitionsResolved = false;
								affectedSections.insert(sec);
							}
						});
					}
					// Stop if we found pattern definitions (that's where searchParentPatterns would have acted)
					if (!sec->patternDefinitions.empty())
						break;
				}
			}
		}

		varMatch.variableReference = nullptr;
	}
	for (PatternMatch &subMatch : match.subMatches) {
		removeVariableReferencesFromMatch(context, reference, subMatch, affectedSections);
	}
}

// Un-resolve a reference: undo all effects and prepare it for re-matching.
// Returns the set of definition sections that had their VL classification affected.
static std::unordered_set<Section *> unresolveReference(
	ParseContext &context, PatternReference *reference,
	std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> &defToRefs
) {
	std::unordered_set<Section *> affectedSections;
	if (!reference->resolved || !reference->match)
		return affectedSections;

	// Remove variable references created from the match
	removeVariableReferencesFromMatch(context, reference, *reference->match, affectedSections);

	// Re-increment VL counts (inverse of decrementVariableLikeCounts done during resolve)
	incrementVariableLikeCounts(reference);

	// Remove from defToRefs tracking
	if (reference->match->matchedEndNode && reference->match->matchedEndNode->matchingDefinition) {
		auto it = defToRefs.find(reference->match->matchedEndNode->matchingDefinition);
		if (it != defToRefs.end()) {
			auto &vec = it->second;
			vec.erase(std::remove(vec.begin(), vec.end(), reference), vec.end());
		}
	}

	// Clear match and mark unresolved
	reference->match = nullptr;
	reference->resolved = false;
	reference->range().section()->incrementUnresolved();

	return affectedSections;
}

// Resolve a list of pattern references against the tree. Returns true if all resolved.
static bool resolveReferences(
	ParseContext &context, std::list<PatternReference *> &references, bool decrementCounts,
	std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> *defToRefs = nullptr
) {
	return std::erase_if(references, [&context, decrementCounts, defToRefs](PatternReference *reference) {
		PatternMatch *match = context.match(reference);
		if (match) {
			reference->resolve(match);
			addVariableReferencesFromMatch(context, reference, *match);
			if (decrementCounts)
				decrementVariableLikeCounts(reference);
			if (defToRefs)
				trackMatchDefinitions(*match, reference, *defToRefs);
		} else if (reference->patternElements.size() == 1 &&
				   reference->patternElements[0].type == PatternElement::Type::VariableLike) {
			reference->patternElements[0].type = PatternElement::Type::Variable;
			reference->resolve();
			reference->range().section()->addVariableReference(
				context, new VariableReference(reference->range(), reference->patternElements[0].text)
			);
			if (decrementCounts)
				decrementVariableLikeCounts(reference);
		}
		return reference->resolved;
	}) > 0;
}

// step 3: loop over code, resolve patterns and build up a pattern tree until all patterns are resolved
bool resolvePatterns(ParseContext &context) {
	std::list<PatternReference *> bodyReferences;
	std::list<PatternReference *> globalReferences;
	std::list<Section *> unResolvedSections;
	context.mainSection->collectPatternReferencesAndSections(bodyReferences, globalReferences, unResolvedSections);
	for (Section *unResolvedSection : unResolvedSections) {
		for (PatternDefinition *unresolvedDefinition : unResolvedSection->patternDefinitions) {
			unresolvedDefinition->patternElements = parsePatternElements(unresolvedDefinition->range.subString);
		}
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

	// Helper: after adding a definition to the tree, find less-specific definitions
	// and unresolve any references that matched them so they can re-match the more specific one.
	auto invalidateStaleMatches = [&](PatternDefinition *definition, SectionType treeType) {
		auto lessSpecific = context.patternTrees[(size_t)treeType]->findLessSpecificDefinitions(definition->patternElements);
		for (PatternDefinition *lessDef : lessSpecific) {
			auto it = definitionToReferences.find(lessDef);
			if (it == definitionToReferences.end())
				continue;
			// Copy the vector since unresolveReference modifies it
			std::vector<PatternReference *> refs = it->second;
			for (PatternReference *ref : refs) {
				if (!ref->resolved)
					continue;
				auto affectedSections = unresolveReference(context, ref, definitionToReferences);
				bodyReferences.push_back(ref);
				// If any ancestor definitions had VL→Variable reverted, re-add their sections
				// for re-resolution so they can re-classify correctly
				for (Section *sec : affectedSections) {
					if (std::find(unResolvedSections.begin(), unResolvedSections.end(), sec) == unResolvedSections.end()) {
						unResolvedSections.push_back(sec);
					}
				}
			}
		}
	};

	// Helper: add a definition to the pattern tree and emit a diagnostic if a duplicate exists.
	auto addDefinitionToTree = [&](PatternDefinition *definition, SectionType treeType) {
		PatternDefinition *existing =
			context.patternTrees[(size_t)treeType]->addPatternPart(definition->patternElements, definition);
		if (existing) {
			Diagnostic diag(Diagnostic::Level::Error, "Duplicate pattern definition", definition->range);
			diag.relatedInfo.push_back({"Conflicts with this definition", existing->range});
			context.diagnostics.push_back(std::move(diag));
		}
		invalidateStaleMatches(definition, treeType);
	};

	for (int resolutionIteration = 0; resolutionIteration < context.options.maxResolutionIterations; resolutionIteration++) {

		bool madeProgress = false;
		size_t sectionsBefore = unResolvedSections.size();

		// each iteration, we go over all sections first
		std::erase_if(unResolvedSections, [&](Section *section) {
			section->patternDefinitionsResolved = true;
			for (PatternDefinition *definition : section->patternDefinitions) {
				if (!definition->resolved) {
					definition->resolved = true;
					forEachLeafElement(definition->patternElements, [&](PatternElement &element) {
						if (element.type == PatternElement::Type::VariableLike) {
							if (definition->patternElements.size() > 1) {
								if (section->variableLikeCounts[element.text] == 0) {
									element.type = PatternElement::Type::Other;
								} else {
									definition->resolved = false;
									section->patternDefinitionsResolved = false;
								}
							}
						}
					});
					if (definition->resolved) {
						SectionType treeType = section->type == SectionType::Class ? SectionType::Expression : section->type;
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
						SectionType treeType = section->type == SectionType::Class ? SectionType::Expression : section->type;
						addDefinitionToTree(definition, treeType);
					}
				}
			}
			return section->patternDefinitionsResolved;
		});

		if (unResolvedSections.size() < sectionsBefore)
			madeProgress = true;

		if (resolveReferences(context, bodyReferences, true, &definitionToReferences))
			madeProgress = true;

		if (unResolvedSections.empty() && bodyReferences.empty())
			break;

		// Deadlock detection: if no progress was made, we have a cycle (self-recursion,
		// mutual recursion, or any dependency loop). Break the cycle by force-resolving
		// all remaining sections — their unresolved VL elements become parameters.
		// The cyclic body references will then resolve against the newly-added definitions
		// in the next iteration.
		if (!madeProgress) {
			if (unResolvedSections.empty())
				break; // no sections to force-resolve and no progress — truly stuck
			for (Section *section : unResolvedSections) {
				section->patternDefinitionsResolved = true;
				for (PatternDefinition *definition : section->patternDefinitions) {
					if (!definition->resolved) {
						definition->resolved = true;
						SectionType treeType = section->type == SectionType::Class ? SectionType::Expression : section->type;
						addDefinitionToTree(definition, treeType);
					}
				}
			}
			unResolvedSections.clear();
		}
	}

	// Phase 2: resolve global references (all definitions are now in the tree)
	for (int resolutionIteration = 0; resolutionIteration < context.options.maxResolutionIterations; resolutionIteration++) {
		resolveReferences(context, globalReferences, false);
		if (globalReferences.empty())
			break;
	}

	if (!unResolvedSections.empty() || !bodyReferences.empty() || !globalReferences.empty()) {
		for (Section *sec : unResolvedSections) {
			for (PatternDefinition *def : sec->patternDefinitions) {
				context.diagnostics.push_back(Diagnostic(
					Diagnostic::Level::Error,
					"This pattern definition couldn't be resolved: " + (std::string)def->range.subString, def->range
				));
			}
		}
		for (PatternReference *reference : bodyReferences)
			context.diagnostics.push_back(
				Diagnostic(Diagnostic::Level::Error, "This pattern couldn't be resolved", reference->range())
			);
		for (PatternReference *reference : globalReferences)
			context.diagnostics.push_back(
				Diagnostic(Diagnostic::Level::Error, "This pattern couldn't be resolved", reference->range())
			);
		return false;
	}

	// Phase 3: Resolve precedence declarations and re-match affected references
	{
		// Resolve before:/after: signature strings to pattern definitions in the expression trie
		auto resolveSignature = [&](const std::string &signature) -> PatternDefinition * {
			std::string converted = signature;
			for (char &c : converted) {
				if (c == '$')
					c = argumentChar;
			}
			auto elements = getPatternElements(converted);
			PatternTreeNode *node = context.patternTrees[(int)SectionType::Expression];
			for (const auto &elem : elements) {
				if (!node)
					return nullptr;
				if (elem.type == PatternElement::Type::Variable) {
					node = node->argumentChild;
				} else {
					auto it = node->literalChildren.find(elem.text);
					node = (it != node->literalChildren.end()) ? it->second : nullptr;
				}
			}
			return node ? node->matchingDefinition : nullptr;
		};

		// Collect precedence edges: higher → lower (higher prec = evaluated first)
		struct PrecedenceEdge {
			PatternDefinition *higher;
			PatternDefinition *lower;
		};
		std::vector<PrecedenceEdge> edges;
		std::unordered_set<PatternDefinition *> involvedDefs;

		// Virtual sentinel for "default" precedence — expression patterns without explicit
		// precedence get this level, which is below all operators that declare "before: default".
		PatternDefinition defaultSentinel(Range(), nullptr);
		bool hasDefaultPrecedence = false;

		std::function<bool(Section *)> collectPrecedence = [&](Section *section) -> bool {
			if (!section->beforePatterns.empty() || !section->afterPatterns.empty()) {
				for (PatternDefinition *def : section->patternDefinitions) {
					involvedDefs.insert(def);

					// before: B means "this definition evaluates before B" = higher precedence
					for (const std::string &beforeStr : section->beforePatterns) {
						if (beforeStr == "default") {
							hasDefaultPrecedence = true;
							involvedDefs.insert(&defaultSentinel);
							edges.push_back({def, &defaultSentinel});
							continue;
						}
						PatternDefinition *target = resolveSignature(beforeStr);
						if (!target) {
							context.diagnostics.push_back(
								Diagnostic(Diagnostic::Level::Error, "Precedence target not found: " + beforeStr, def->range)
							);
							return false;
						}
						involvedDefs.insert(target);
						edges.push_back({def, target});
					}

					// after: A means "this definition evaluates after A" = lower precedence
					for (const std::string &afterStr : section->afterPatterns) {
						if (afterStr == "default") {
							hasDefaultPrecedence = true;
							involvedDefs.insert(&defaultSentinel);
							edges.push_back({&defaultSentinel, def});
							continue;
						}
						PatternDefinition *target = resolveSignature(afterStr);
						if (!target) {
							context.diagnostics.push_back(
								Diagnostic(Diagnostic::Level::Error, "Precedence target not found: " + afterStr, def->range)
							);
							return false;
						}
						involvedDefs.insert(target);
						edges.push_back({target, def});
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

		if (!involvedDefs.empty()) {
			// Topological sort (Kahn's algorithm) to assign precedence levels
			std::unordered_map<PatternDefinition *, std::vector<PatternDefinition *>> adjList;
			std::unordered_map<PatternDefinition *, int> inDegree;
			for (PatternDefinition *def : involvedDefs)
				inDegree[def] = 0;
			for (auto &edge : edges) {
				adjList[edge.higher].push_back(edge.lower);
				inDegree[edge.lower]++;
			}

			std::queue<PatternDefinition *> topoQueue;
			for (auto &[def, deg] : inDegree) {
				if (deg == 0)
					topoQueue.push(def);
			}

			// BFS wave-based topological sort: nodes in the same wave get the same precedence level.
			// This ensures operators like * and / (no edge between them) share the same level,
			// enforcing left-to-right associativity for same-precedence operators.
			size_t processedCount = 0;
			int currentLevel = (int)involvedDefs.size();
			while (!topoQueue.empty()) {
				size_t waveSize = topoQueue.size();
				for (size_t i = 0; i < waveSize; i++) {
					PatternDefinition *def = topoQueue.front();
					topoQueue.pop();
					def->precedence = currentLevel;
					processedCount++;
					for (PatternDefinition *lower : adjList[def]) {
						if (--inDegree[lower] == 0)
							topoQueue.push(lower);
					}
				}
				currentLevel--;
			}

			if (processedCount != involvedDefs.size()) {
				context.diagnostics.push_back(
					Diagnostic(Diagnostic::Level::Error, "Cycle detected in precedence declarations", Range())
				);
				return false;
			}

			// Store default precedence level for use in pattern matching.
			// Patterns that want lowest precedence (math functions) declare "after: default".
			// Patterns without explicit precedence stay at 0 (bypass precedence checks).
			if (hasDefaultPrecedence && defaultSentinel.precedence > 0) {
				context.defaultPrecedenceLevel = defaultSentinel.precedence;
			}

			// Re-match body references that involve operators with precedence
			std::function<bool(PatternMatch &)> matchInvolvesPrecedence = [&](PatternMatch &match) -> bool {
				if (match.matchedEndNode && match.matchedEndNode->matchingDefinition &&
					match.matchedEndNode->matchingDefinition->precedence > 0)
					return true;
				for (PatternMatch &sub : match.subMatches)
					if (matchInvolvesPrecedence(sub))
						return true;
				return false;
			};

			// Helper to remove variable references from a match (simplified, no VL count changes)
			std::function<void(PatternReference *, PatternMatch &)> removeMatchVarRefs = [&](PatternReference *reference,
																							 PatternMatch &match) {
				Section *refSection = reference->range().section();
				for (VariableMatch &varMatch : match.discoveredVariables) {
					if (!varMatch.variableReference)
						continue;
					const std::string &name = varMatch.variableReference->name;
					auto it = refSection->variableReferences.find(name);
					if (it != refSection->variableReferences.end()) {
						auto &vec = it->second;
						vec.erase(std::remove(vec.begin(), vec.end(), varMatch.variableReference), vec.end());
						if (vec.empty())
							refSection->variableReferences.erase(it);
					}
					auto uit = context.unresolvedVariableReferences.find(name);
					if (uit != context.unresolvedVariableReferences.end()) {
						auto &vec = uit->second;
						vec.erase(std::remove(vec.begin(), vec.end(), varMatch.variableReference), vec.end());
						if (vec.empty())
							context.unresolvedVariableReferences.erase(uit);
					}
					varMatch.variableReference = nullptr;
				}
				for (PatternMatch &sub : match.subMatches)
					removeMatchVarRefs(reference, sub);
			};

			// Collect all pattern references from all sections
			std::vector<PatternReference *> allRefs;
			std::function<void(Section *)> collectRefs = [&](Section *section) {
				allRefs.insert(allRefs.end(), section->patternReferences.begin(), section->patternReferences.end());
				for (Section *child : section->children)
					collectRefs(child);
			};
			collectRefs(context.mainSection);

			// Re-match references that involve operators with precedence
			for (PatternReference *ref : allRefs) {
				if (!ref->resolved || !ref->match)
					continue;
				if (!matchInvolvesPrecedence(*ref->match))
					continue;

				// Remove variable references from old match
				removeMatchVarRefs(ref, *ref->match);

				// Re-match with precedence constraints active
				PatternMatch *newMatch = context.match(ref);
				ref->match = newMatch;
				if (newMatch)
					addVariableReferencesFromMatch(context, ref, *newMatch);
			}
		}
	}

	// All patterns resolved — expand expressions and resolve variable references
	for (CodeLine *line : context.codeLines) {
		if (line->expression)
			expandExpression(line->expression, line->section);
	}
	for (auto &[name, references] : context.unresolvedVariableReferences) {
		bool isGlobal = context.declaredGlobalVariables.contains(name);

		std::unordered_map<Section *, Section *> sectionToHighest;
		for (VariableReference *ref : references) {
			Section *sec = ref->range.section();
			if (sectionToHighest.contains(sec))
				continue;
			Section *highest = sec;

			// Find the enclosing function (Expression/Effect section)
			Section *functionScope = nullptr;
			for (Section *a = sec; a; a = a->parent) {
				if ((a->type == SectionType::Expression || a->type == SectionType::Effect) && !a->isMacro) {
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
		for (auto &[highestSection, groupRefs] : groups) {
			VariableReference *definition = *std::min_element(groupRefs.begin(), groupRefs.end(), [](auto *a, auto *b) {
				return a->range.line->mergedLineIndex < b->range.line->mergedLineIndex;
			});

			// This group is the global one if the variable is declared global and
			// the group's highest section is at the top level (no enclosing function)
			bool groupIsGlobal = false;
			if (isGlobal) {
				groupIsGlobal = true;
				for (Section *a = highestSection; a; a = a->parent) {
					if ((a->type == SectionType::Expression || a->type == SectionType::Effect) && !a->isMacro) {
						groupIsGlobal = false;
						break;
					}
				}
			}

			highestSection->variableDefinitions[name] = definition;
			highestSection->variables[name] = new Variable(name, definition, groupIsGlobal);
			for (VariableReference *ref : groupRefs) {
				if (ref != definition)
					ref->definition = definition;
			}
		}
	}
	return true;
}
