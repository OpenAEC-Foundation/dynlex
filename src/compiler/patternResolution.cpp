#include "classSection.h"
#include "compiler.h"
#include "function.h"
#include "intrinsicInfo.h"
#include "patternElement.h"
#include "patternTreeNode.h"
#include "transformedPattern.h"
#include "type.h"
#include "variable.h"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <list>
#include <ranges>
#include <sstream>
#include <tuple>
#include <unordered_set>

namespace {
static std::tuple<int, int, int, std::string> definitionSortKey(const PatternDefinition *def) {
	if (!def)
		return {INT_MAX, INT_MAX, INT_MAX, ""};
	if (!def->range.line)
		return {INT_MAX - 1, def->range.start(), def->range.end(), def->toString()};
	return {def->range.line->mergedLineIndex, def->range.start(), def->range.end(), def->toString()};
}

static bool definitionComesBefore(const PatternDefinition *a, const PatternDefinition *b) {
	return definitionSortKey(a) < definitionSortKey(b);
}

static std::tuple<int, int, int, std::string> sectionSortKey(const Section *section) {
	if (!section)
		return {INT_MAX, INT_MAX, INT_MAX, ""};

	int mergedLineIndex = INT_MAX - 1;
	int sourceLineIndex = INT_MAX - 1;
	std::string sectionText = section->toString();
	if (section->openingLine) {
		mergedLineIndex = section->openingLine->mergedLineIndex;
		sourceLineIndex = section->openingLine->sourceFileLineIndex;
		sectionText = std::string(section->openingLine->patternText);
	}

	return {mergedLineIndex, sourceLineIndex, static_cast<int>(section->type), sectionText};
}

static bool sectionComesBefore(const Section *a, const Section *b) { return sectionSortKey(a) < sectionSortKey(b); }

static std::tuple<int, int, int, std::string> referenceSortKey(const PatternReference *reference) {
	if (!reference)
		return {INT_MAX, INT_MAX, INT_MAX, ""};
	const Range &r = reference->range();
	if (!r.line)
		return {INT_MAX - 1, r.start(), r.end(), reference->pattern.text};
	return {r.line->mergedLineIndex, r.start(), r.end(), reference->pattern.text};
}

static bool referenceComesBefore(const PatternReference *a, const PatternReference *b) {
	return referenceSortKey(a) < referenceSortKey(b);
}

static bool resolutionTraceEnabled() {
	static const bool enabled = []() {
		const char *env = std::getenv("DYNLEX_TRACE_RESOLUTION");
		if (!env)
			return false;
		std::string value(env);
		return value == "1" || value == "true" || value == "TRUE" || value == "yes" || value == "YES";
	}();
	return enabled;
}

static std::string definitionTraceId(const PatternDefinition *def) {
	if (!def)
		return "def:<null>";
	std::ostringstream out;
	out << "def:" << def->toString();
	if (def->range.line)
		out << "@L" << def->range.line->mergedLineIndex;
	else
		out << "@L?";
	return out.str();
}

static std::string referenceTraceId(const PatternReference *reference) {
	if (!reference)
		return "ref:<null>";
	std::ostringstream out;
	out << "ref:" << reference->pattern.text;
	if (reference->range().line)
		out << "@L" << reference->range().line->mergedLineIndex;
	else
		out << "@L?";
	return out.str();
}

static std::string sectionTraceId(const Section *section) {
	if (!section)
		return "sec:<null>";
	std::ostringstream out;
	out << "sec:" << section->toString();
	if (section->openingLine)
		out << "@L" << section->openingLine->mergedLineIndex;
	else
		out << "@L?";
	return out.str();
}

static void traceResolution(const std::string &message) {
	if (!resolutionTraceEnabled())
		return;
	std::cerr << "[res] " << message << '\n';
}

static void appendUniqueSection(std::vector<Section *> &sections, Section *section) {
	if (!section)
		return;
	if (std::find(sections.begin(), sections.end(), section) == sections.end())
		sections.push_back(section);
}
} // namespace

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

void expandMatch(Function *rootFunction, Function *expr, PatternMatch *match) {
	expr->arguments = match->arguments;
	// move arguments to the appropriate submatches
	expr->kind = Function::Kind::PatternCall;
	expr->patternMatch = match;
	for (const PatternMatch &subMatch : match->subMatches) {
		Function *arg = new Function();
		arg->isSubMatch = true;
		arg->range = Range(
			expr->range.line, rootFunction->range.start() + subMatch.lineStartPos,
			rootFunction->range.start() + subMatch.lineEndPos
		);
		expandMatch(rootFunction, arg, const_cast<PatternMatch *>(&subMatch));
		expr->arguments.push_back(arg);
	}

	// Handle discoveredVariables - add Variable functions using stored references
	for (const VariableMatch &varMatch : match->discoveredVariables) {
		Function *arg = new Function();
		arg->kind = Function::Kind::Variable;
		arg->variable = varMatch.variableReference;
		arg->range = varMatch.variableReference->range;
		expr->arguments.push_back(arg);
	}

	// Handle discoveredWords - add string Literal functions
	for (const WordMatch &wordMatch : match->discoveredWords) {
		Function *arg = new Function();
		arg->kind = Function::Kind::Literal;
		arg->literalValue = wordMatch.text;
		arg->range = Range(
			rootFunction->range.line, rootFunction->range.start() + wordMatch.lineStartPos,
			rootFunction->range.start() + wordMatch.lineEndPos
		);
		expr->arguments.push_back(arg);
	}
}

// Recursively expand pending functions to their resolved forms
void expandFunction(Function *expr, Section *section) {
	if (!expr)
		return;

	// Expand children first
	for (Function *arg : expr->arguments) {
		expandFunction(arg, section);
	}

	// If this is a pending function, resolve it
	// we can assume that expr->patternReference is set, since pending functions are only expanded after complete pattern
	// resolution
	if (expr->kind == Function::Kind::Pending) {
		PatternReference *ref = expr->patternReference;
		if (ref->match) {
			expandMatch(expr, expr, ref->match);
		} else if (ref->patternElements.size() == 1 && ref->patternElements[0].type == PatternElement::Type::Variable) {
			// Resolved to a variable reference
			expr->kind = Function::Kind::Variable;
			// Find the variable reference in the section
			std::string varName = ref->patternElements[0].text;
			auto it = section->variableReferences.find(varName);
			if (it != section->variableReferences.end() && !it->second.empty()) {
				expr->variable = it->second.front();
			}
		} else if (expr->arguments.size() == 1 && expr->arguments[0]->kind == Function::Kind::IntrinsicCall) {
			// If the pattern is just an argument placeholder and we have a single intrinsic call,
			// promote the intrinsic to be this function
			Function *intrinsic = expr->arguments[0];
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
	if (match.matchedEndNode && !match.matchedEndNode->matchingDefinitions.empty()) {
		for (auto *def : match.matchedEndNode->matchingDefinitions) {
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
	if (match.matchedEndNode && !match.matchedEndNode->matchingDefinitions.empty()) {
		for (auto *def : match.matchedEndNode->matchingDefinitions) {
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

// Remove VariableReferences created from a match, undoing addVariableReferencesFromMatch and searchParentPatterns effects.
// If any ancestor definitions had VL→Variable promotions reverted, their sections are added to affectedSections.
static void removeVariableReferencesFromMatch(
	ParseContext &context, PatternReference *reference, PatternMatch &match, std::vector<Section *> &affectedSections
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
							VariableReference *definitionRef = defIt->second;
							// Remove the definition VarRef from variableReferences too
							auto vit = sec->variableReferences.find(name);
							if (vit != sec->variableReferences.end()) {
								auto &vec = vit->second;
								vec.erase(std::remove(vec.begin(), vec.end(), definitionRef), vec.end());
								if (vec.empty())
									sec->variableReferences.erase(vit);
							}
							sec->variableDefinitions.erase(defIt);
						}

					// Revert Variable→VariableLike in pattern definitions and mark for re-resolution.
					// Must remove from tree BEFORE changing element types (tree was built with old types).
					for (PatternDefinition *def : sec->patternDefinitions) {
						bool needsReResolution = false;
						forEachLeafElement(def->patternElements, [&](DefinitionPatternElement &element) {
							// Only revert unconstrained Variables — typed arguments ({type:name})
							// were never VariableLike and must not be reverted.
							if (element.type == PatternElement::Type::Variable && element.text == name &&
								element.typeConstraintName.empty())
								needsReResolution = true;
						});
						if (needsReResolution && def->resolved) {
							// Remove from tree while elements still reflect the old path
							SectionType treeType = sec->type == SectionType::Class ? SectionType::Function : sec->type;
							context.patternTrees[(size_t)treeType]->removePatternPart(def->patternElements, def);
						}
						// Now revert element types (only unconstrained — typed arguments stay Variable)
						forEachLeafElement(def->patternElements, [&](DefinitionPatternElement &element) {
							if (element.type == PatternElement::Type::Variable && element.text == name &&
								element.typeConstraintName.empty()) {
								element.type = PatternElement::Type::VariableLike;
								def->resolved = false;
								sec->patternDefinitionsResolved = false;
								appendUniqueSection(affectedSections, sec);
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
static std::vector<Section *> unresolveReference(
	ParseContext &context, PatternReference *reference,
	std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> &defToRefs
) {
	std::vector<Section *> affectedSections;
	if (!reference->resolved || !reference->match)
		return affectedSections;

	// Remove variable references created from the match
	removeVariableReferencesFromMatch(context, reference, *reference->match, affectedSections);

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

// Resolve a list of pattern references against the tree. Returns true if all resolved.
static bool resolveReferences(
	ParseContext &context, std::list<PatternReference *> &references, bool decrementCounts,
	std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> *defToRefs = nullptr,
	const char *phase = "body"
) {
	return std::erase_if(references, [&context, decrementCounts, defToRefs, phase](PatternReference *reference) {
		PatternMatch *match = context.match(reference);
		if (match) {
			// Multi-word reference matched a pattern in the tree
			reference->resolve(match);
			addVariableReferencesFromMatch(context, reference, *match);
			if (decrementCounts)
				decrementVariableLikeCounts(reference);
			if (defToRefs)
				trackMatchDefinitions(*match, reference, *defToRefs);
			traceResolution(std::string(phase) + " resolved " + referenceTraceId(reference));
		} else if (reference->patternElements.size() == 1 &&
				   reference->patternElements[0].type == PatternElement::Type::VariableLike) {
			// Single-word reference that didn't match any pattern — must be a variable.
			// This confirms that the word is used as a parameter in the body, so promote
			// any matching VariableLike elements in ancestor definitions from ambiguous (VL)
			// to confirmed parameter (Variable). Without this, the ancestor definition stays
			// unresolved because VL counts never reach 0 — the decrement checks for VL type
			// but we've already reclassified the element as Variable.
			const std::string &varName = reference->patternElements[0].text;
			Section *sec = reference->range().section();
			while (sec) {
				for (PatternDefinition *def : sec->patternDefinitions) {
					forEachLeafElement(def->patternElements, [&](PatternElement &elem) {
						if (elem.type == PatternElement::Type::VariableLike && elem.text == varName)
							elem.type = PatternElement::Type::Variable;
					});
				}
				sec = sec->parent;
			}
			reference->patternElements[0].type = PatternElement::Type::Variable;
			reference->resolve();
			reference->range().section()->addVariableReference(
				context, context.createVariableReference(reference->range(), reference->patternElements[0].text)
			);
			if (decrementCounts)
				decrementVariableLikeCounts(reference);
			traceResolution(std::string(phase) + " resolved-as-variable " + referenceTraceId(reference));
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
	traceResolution(
		"start sections=" + std::to_string(unResolvedSections.size()) + " body_refs=" + std::to_string(bodyReferences.size()) +
		" global_refs=" + std::to_string(globalReferences.size())
	);
	bool hadPatternParseError = false;
	for (Section *unResolvedSection : unResolvedSections) {
		for (PatternDefinition *unresolvedDefinition : unResolvedSection->patternDefinitions) {
			std::string errorMessage;
			size_t errorOffset = 0;
			unresolvedDefinition->patternElements =
				parsePatternElements(unresolvedDefinition->range.subString, 0, &errorMessage, &errorOffset);
			if (!errorMessage.empty()) {
				hadPatternParseError = true;
				size_t diagnosticEnd = std::min(errorOffset + 1, unresolvedDefinition->range.subString.size());
				context.diagnostics.push_back(Diagnostic(
					Diagnostic::Level::Error, errorMessage, unresolvedDefinition->range.subRange(errorOffset, diagnosticEnd)
				));
			}
		}
	}
	if (hadPatternParseError)
		return false;
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
	bool staleInvalidationOccurred = false;
	std::vector<Section *> pendingRequeueSections;

	// Helper: after adding a definition to the tree, find less-specific definitions
	// and unresolve any references that matched them so they can re-match the more specific one.
	auto invalidateStaleMatches = [&](PatternDefinition *definition, SectionType treeType) {
		auto lessSpecific = context.patternTrees[(size_t)treeType]->findLessSpecificDefinitions(definition->patternElements);
		std::sort(lessSpecific.begin(), lessSpecific.end(), definitionComesBefore);
		traceResolution("invalidate base=" + definitionTraceId(definition) + " candidates=" + std::to_string(lessSpecific.size()));
		for (PatternDefinition *lessDef : lessSpecific) {
			auto it = definitionToReferences.find(lessDef);
			if (it == definitionToReferences.end())
				continue;
			// Copy the vector since unresolveReference modifies it
			std::vector<PatternReference *> refs = it->second;
			std::sort(refs.begin(), refs.end(), referenceComesBefore);
			for (PatternReference *ref : refs) {
				if (!ref->resolved)
					continue;
				staleInvalidationOccurred = true;
				auto affectedSections = unresolveReference(context, ref, definitionToReferences);
				std::sort(affectedSections.begin(), affectedSections.end(), sectionComesBefore);
				bodyReferences.push_back(ref);
				// If any ancestor definitions had VL→Variable reverted, re-add their sections
				// for re-resolution so they can re-classify correctly
				for (Section *sec : affectedSections) {
					appendUniqueSection(pendingRequeueSections, sec);
				}
			}
		}
	};

	// Helper: add a definition to the pattern tree and emit a diagnostic if a duplicate exists.
	auto addDefinitionToTree = [&](PatternDefinition *definition, SectionType treeType) {
		PatternDefinition *existing =
			context.patternTrees[(size_t)treeType]->addPatternPart(definition->patternElements, definition);
		traceResolution("add " + definitionTraceId(definition) + " tree=" + std::to_string((int)treeType));
		if (existing) {
			Diagnostic diag(Diagnostic::Level::Error, "Duplicate pattern definition", definition->range);
			diag.relatedInfo.push_back({"Conflicts with this definition", existing->range});
			context.diagnostics.push_back(std::move(diag));
			traceResolution("duplicate " + definitionTraceId(definition) + " vs " + definitionTraceId(existing));
		}
		invalidateStaleMatches(definition, treeType);
	};

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
						SectionType treeType = section->type == SectionType::Class ? SectionType::Function : section->type;
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
						SectionType treeType = section->type == SectionType::Class ? SectionType::Function : section->type;
						addDefinitionToTree(definition, treeType);
					}
				}
			}
			return section->patternDefinitionsResolved;
		});
		if (!pendingRequeueSections.empty()) {
			std::sort(pendingRequeueSections.begin(), pendingRequeueSections.end(), sectionComesBefore);
			for (Section *sec : pendingRequeueSections) {
				if (std::find(unResolvedSections.begin(), unResolvedSections.end(), sec) == unResolvedSections.end())
					unResolvedSections.push_back(sec);
			}
			pendingRequeueSections.clear();
		}

		if (unResolvedSections.size() < sectionsBefore)
			madeProgress = true;
		if (staleInvalidationOccurred)
			madeProgress = true;

		if (resolveReferences(context, bodyReferences, true, &definitionToReferences, "body"))
			madeProgress = true;

		// Resolve type constraints on definition elements ({type:name} syntax).
		// Walk all definitions and resolve typeConstraintName strings to DataTypes
		// by looking up the type name in the function pattern tree.
		{
			PatternTreeNode *exprTree = context.patternTrees[(size_t)SectionType::Function];
			std::function<void(Section *)> resolveTypeConstraints = [&](Section *section) {
				for (PatternDefinition *def : section->patternDefinitions) {
					for (auto &elem : def->patternElements) {
						if (elem.typeConstraintName.empty() || elem.resolvedTypeConstraint.isDeduced())
							continue;
						PatternTreeNode *node = exprTree;
						std::string_view remaining = elem.typeConstraintName;
						while (!remaining.empty() && node) {
							size_t space = remaining.find(' ');
							std::string_view word = (space != std::string_view::npos) ? remaining.substr(0, space) : remaining;
							auto it = node->literalChildren.find(std::string(word));
							node = (it != node->literalChildren.end()) ? it->second : nullptr;
							remaining = (space != std::string_view::npos) ? remaining.substr(space + 1) : std::string_view{};
						}
						if (!node || node->matchingDefinitions.empty())
							continue;
						for (auto *d : node->matchingDefinitions) {
							if (d->section->type == SectionType::Class && !d->section->isMacro) {
								auto *classSec = static_cast<ClassSection *>(d->section);
								elem.resolvedTypeConstraint = {DataType::Kind::Class, 0, 0, classSec->classDefinition};
								break;
							}
							if (d->section->isMacro) {
								// Macro type (e.g., integer, float, boolean) — evaluate the type intrinsic
								for (Section *child : d->section->children) {
									for (CodeLine *cl : child->codeLines) {
										if (cl->function && cl->function->kind == Function::Kind::IntrinsicCall &&
											intrinsicKind(cl->function->intrinsicName) == IntrinsicKind::Type) {
											auto &args = cl->function->arguments;
											if (auto *kindStr = std::get_if<std::string>(&args[1]->literalValue)) {
												if (*kindStr == "int") {
													int bits = 32;
													if (args.size() >= 3)
														if (auto *b = std::get_if<double>(&args[2]->literalValue))
															bits = (int)*b;
													elem.resolvedTypeConstraint = {DataType::Kind::Int, bits / 8};
												} else if (*kindStr == "float") {
													int bits = 64;
													if (args.size() >= 3)
														if (auto *b = std::get_if<double>(&args[2]->literalValue))
															bits = (int)*b;
													elem.resolvedTypeConstraint = {DataType::Kind::Float, bits / 8};
												} else if (*kindStr == "bool") {
													elem.resolvedTypeConstraint = {DataType::Kind::Bool};
												} else if (*kindStr == "string") {
													elem.resolvedTypeConstraint = {DataType::Kind::Int, 1};
													elem.resolvedTypeConstraint.pointerDepth = 1;
												}
											}
											break;
										}
									}
									if (elem.resolvedTypeConstraint.isDeduced())
										break;
								}
								if (elem.resolvedTypeConstraint.isDeduced())
									break;
							}
						}
						if (elem.resolvedTypeConstraint.isDeduced())
							madeProgress = true;
					}
				}
				for (Section *child : section->children)
					resolveTypeConstraints(child);
			};
			resolveTypeConstraints(context.mainSection);
		}

		if (unResolvedSections.empty() && bodyReferences.empty())
			break;

		// Deadlock detection: if no progress was made, we have a cycle (self-recursion,
		// mutual recursion, or any dependency loop). Break the cycle by force-resolving
		// all remaining sections — their unresolved VL elements become parameters.
		// The cyclic body references will then resolve against the newly-added definitions
		// in the next iteration.
		if (!madeProgress) {
			traceResolution("iter " + std::to_string(resolutionIteration) + " deadlock force-resolve");
			if (unResolvedSections.empty())
				break; // no sections to force-resolve and no progress — truly stuck
			// Force-resolve all remaining sections by adding their definitions as-is.
			// Copy the list first because addDefinitionToTree may trigger invalidations
			// that re-add sections to unResolvedSections.
			std::list<Section *> toForceResolve = std::move(unResolvedSections);
			unResolvedSections.clear();
			for (Section *section : toForceResolve) {
				section->patternDefinitionsResolved = true;
				for (PatternDefinition *definition : section->patternDefinitions) {
					if (!definition->resolved) {
						definition->resolved = true;
						SectionType treeType = section->type == SectionType::Class ? SectionType::Function : section->type;
						addDefinitionToTree(definition, treeType);
					}
				}
			}
			// Sections may have been re-added by invalidation cascades — continue the loop
		}
	}

	// If we exhausted iterations but still have unresolved definition sections,
	// do one deterministic force-resolve sweep and rematch body references.
	// This avoids platform-dependent oscillation where stale invalidations keep
	// progress non-zero while never reaching a fixed point.
	if (!unResolvedSections.empty()) {
		std::vector<Section *> remainingSections(unResolvedSections.begin(), unResolvedSections.end());
		std::sort(remainingSections.begin(), remainingSections.end(), sectionComesBefore);
		unResolvedSections.clear();

		for (Section *section : remainingSections) {
			section->patternDefinitionsResolved = true;
			for (PatternDefinition *definition : section->patternDefinitions) {
				if (!definition->resolved) {
					definition->resolved = true;
					SectionType treeType = section->type == SectionType::Class ? SectionType::Function : section->type;
					addDefinitionToTree(definition, treeType);
				}
			}
		}

		for (int rematchIteration = 0; rematchIteration < context.options.maxResolutionIterations; rematchIteration++) {
			if (!resolveReferences(context, bodyReferences, true, &definitionToReferences))
				break;
			if (bodyReferences.empty())
				break;
		}
	}

	// Phase 2: resolve global references (all definitions are now in the tree)
	for (int resolutionIteration = 0; resolutionIteration < context.options.maxResolutionIterations; resolutionIteration++) {
		resolveReferences(context, globalReferences, false, nullptr, "global");
		if (globalReferences.empty())
			break;
	}

	if (!unResolvedSections.empty() || !bodyReferences.empty() || !globalReferences.empty()) {
		traceResolution(
			"failed sections=" + std::to_string(unResolvedSections.size()) + " body_refs=" +
			std::to_string(bodyReferences.size()) + " global_refs=" + std::to_string(globalReferences.size())
		);
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

	// Validate type constraints — report unresolved ones as errors
	{
		std::function<void(Section *)> validateTypeConstraints = [&](Section *section) {
			for (PatternDefinition *def : section->patternDefinitions) {
				for (auto &elem : def->patternElements) {
					if (!elem.typeConstraintName.empty() && !elem.resolvedTypeConstraint.isDeduced()) {
						context.diagnostics.push_back(Diagnostic(
							Diagnostic::Level::Error,
							"Type constraint '" + elem.typeConstraintName + "' does not refer to a known type", def->range
						));
					}
				}
			}
			for (Section *child : section->children)
				validateTypeConstraints(child);
		};
		validateTypeConstraints(context.mainSection);
	}

	// Phase 3: Resolve precedence declarations and re-match affected references
	{
		// Resolve before:/after: signature strings to pattern definitions in the function trie
		auto resolveSignature = [&](const std::string &signature) -> PatternDefinition * {
			return findDefinitionBySignature(context, SectionType::Function, signature);
		};

		// Collect precedence edges: higher → lower (higher prec = evaluated first)
		struct PrecedenceEdge {
			PatternDefinition *higher;
			PatternDefinition *lower;
		};
		std::vector<PrecedenceEdge> edges;
		std::unordered_set<PatternDefinition *> involvedDefs;

		// Virtual sentinel for "default" precedence — function patterns without explicit
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

			std::vector<PatternDefinition *> zeroInDegree;
			zeroInDegree.reserve(inDegree.size());
			for (auto &[def, deg] : inDegree) {
				if (deg == 0) {
					zeroInDegree.push_back(def);
				}
			}
			std::sort(zeroInDegree.begin(), zeroInDegree.end(), definitionComesBefore);

			// BFS wave-based topological sort: nodes in the same wave get the same precedence level.
			// This ensures operators like * and / (no edge between them) share the same level,
			// enforcing left-to-right associativity for same-precedence operators.
			size_t processedCount = 0;
			int currentLevel = (int)involvedDefs.size();
			std::vector<PatternDefinition *> currentWave = zeroInDegree;
			while (!currentWave.empty()) {
				std::vector<PatternDefinition *> nextWave;
				for (PatternDefinition *def : currentWave) {
					def->precedence = currentLevel;
					processedCount++;
					for (PatternDefinition *lower : adjList[def]) {
						if (--inDegree[lower] == 0)
							nextWave.push_back(lower);
					}
				}
				std::sort(nextWave.begin(), nextWave.end(), definitionComesBefore);
				nextWave.erase(std::unique(nextWave.begin(), nextWave.end()), nextWave.end());
				currentWave = std::move(nextWave);
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

			// Precedence re-matching is NOT done here — operand reordering in type inference
			// handles grouping selection using def->precedence values.
		}
	}

	// All patterns resolved — expand functions and resolve variable references
	for (CodeLine *line : context.codeLines) {
		if (line->function)
			expandFunction(line->function, line->section);
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
				if (a->type == SectionType::Function && !a->isMacro) {
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
					if (a->type == SectionType::Function && !a->isMacro) {
						groupIsGlobal = false;
						break;
					}
				}
			}

				auto existingDefinition = highestSection->variableDefinitions.find(name);
				if (existingDefinition != highestSection->variableDefinitions.end() &&
					existingDefinition->second != definition) {
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
				highestSection->variables[name] = new Variable(name, definition, groupIsGlobal);
			for (VariableReference *ref : groupRefs) {
				if (ref != definition)
					ref->definition = definition;
			}
		}
	}
	return true;
}
