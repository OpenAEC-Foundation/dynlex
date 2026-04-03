#include "classSection.h"
#include "compiler.h"
#include "expression.h"
#include "intrinsicInfo.h"
#include "patternElement.h"
#include "patternTreeNode.h"
#include "transformedPattern.h"
#include "type.h"
#include "variable.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdlib>
#include <functional>
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

static bool sectionContainsOrIsAncestorOf(Section *section, Section *candidate) {
	for (Section *current = candidate; current; current = current->parent) {
		if (current == section)
			return true;
	}
	return false;
}

static Section *
findDefinitionOwnerSection(Section *startSection, const std::string &name, const VariableReference *definitionReference) {
	for (Section *section = startSection; section; section = section->parent) {
		auto it = section->variableDefinitions.find(name);
		if (it != section->variableDefinitions.end() && it->second == definitionReference)
			return section;
	}
	return nullptr;
}

static bool sectionSubtreeHasBoundReferenceToDefinition(
	Section *section, const std::string &name, const VariableReference *definitionReference
) {
	if (!section)
		return false;

	auto it = section->variableReferences.find(name);
	if (it != section->variableReferences.end()) {
		for (VariableReference *reference : it->second) {
			if (!reference || reference == definitionReference)
				continue;
			if (reference->definition == definitionReference)
				return true;
		}
	}

	for (Section *child : section->children) {
		if (sectionSubtreeHasBoundReferenceToDefinition(child, name, definitionReference))
			return true;
	}

	return false;
}

static void collectPromotablePatternNames(
	Section *section, std::unordered_set<std::string> &names, std::unordered_set<Section *> &visited
) {
	if (!section || !visited.insert(section).second)
		return;
	for (PatternDefinition *definition : section->patternDefinitions) {
		forEachLeafElement(definition->patternElements, [&](DefinitionPatternElement &element) {
			if (element.type == PatternElement::Type::VariableLike && canPromoteVariableLikeElement(element))
				names.insert(element.text);
		});
	}
	for (Section *child : section->children)
		collectPromotablePatternNames(child, names, visited);
}

static bool
promotePatternNameInSectionChain(ParseContext &context, Section *section, const std::string &name, const Range &useRange) {
	for (Section *current = section; current; current = current->parent) {
		bool foundInCurrent = false;
		for (PatternDefinition *definition : current->patternDefinitions) {
			visitPatternNameWithFoundState(definition->patternElements, name, false, [&](DefinitionPatternElement &element) {
				if (element.type != PatternElement::Type::Variable && element.type != PatternElement::Type::VariableLike)
					return false;
				if (element.type != PatternElement::Type::VariableLike) {
					foundInCurrent = true;
					return true;
				}
				if (!canPromoteVariableLikeElement(element))
					return false;
				element.promotedFromVariableLike = true;
				if (!element.firstImplicitPromotionUseRange.line)
					element.firstImplicitPromotionUseRange = useRange;
				element.type = PatternElement::Type::Variable;
				foundInCurrent = true;
				return true;
			});
		}
		if (!foundInCurrent)
			continue;
		if (!current->variableDefinitions.contains(name)) {
			for (PatternDefinition *definition : current->patternDefinitions) {
				bool created = visitPatternNameWithFoundState(
					definition->patternElements, name, false,
					[&](DefinitionPatternElement &element) {
					if (element.type != PatternElement::Type::Variable && element.type != PatternElement::Type::Word)
						return false;
					VariableReference *varRef = context.createVariableReference(
						Range(
							definition->range.line, definition->range.start() + element.startPos,
							definition->range.start() + element.startPos + element.text.length()
						),
						element.text
					);
					current->variableDefinitions[element.text] = varRef;
					current->variableReferences[element.text].push_back(varRef);
					return true;
				}
				);
				if (created)
					break;
			}
		}
		return true;
	}
	return false;
}

static bool isInternalSection(Section *section) {
	if (!section)
		return false;
	for (CodeLine *line : section->codeLines) {
		if (line && line->sourceFile && !line->sourceFile->uri.empty())
			return isInternalSourcePath(line->sourceFile->uri);
	}
	return section->openingLine && section->openingLine->sourceFile &&
		   isInternalSourcePath(section->openingLine->sourceFile->uri);
}

static bool isFlexFunctionDefinition(const PatternDefinition *definition) {
	assert(definition && "flex specialization checks require a real pattern definition");
	assert(definition->section && "pattern definition must belong to a section");
	return definition->section->type == SectionType::Function && definition->section->isFlex;
}

static bool definitionHasTypeConstraints(const PatternDefinition &definition) {
	bool hasTypeConstraint = false;
	std::function<void(const std::vector<DefinitionPatternElement> &)> visit =
		[&](const std::vector<DefinitionPatternElement> &elements) {
		for (const DefinitionPatternElement &element : elements) {
			if (element.type == PatternElement::Type::Choice) {
				for (const auto &alternative : element.alternatives)
					visit(alternative);
				continue;
			}
			if (element.type == PatternElement::Type::Variable && !element.typeConstraintName.empty())
				hasTypeConstraint = true;
		}
	};
	visit(definition.patternElements);
	return hasTypeConstraint;
}

static void
appendImplicitPromotionDuplicateDetails(Diagnostic &diagnostic, const PatternDefinition *left, const PatternDefinition *right);

struct DefinitionConflict {
	enum class Kind {
		TypedFlexParameters,
		NonFlexSpecializesFlex,
		DuplicatePatternDefinition,
	};

	Kind kind;
	PatternDefinition *primary;
	PatternDefinition *related{};
};

static bool definitionConflictComesBefore(const DefinitionConflict &left, const DefinitionConflict &right) {
	if (definitionComesBefore(left.primary, right.primary))
		return true;
	if (definitionComesBefore(right.primary, left.primary))
		return false;
	if (definitionComesBefore(left.related, right.related))
		return true;
	if (definitionComesBefore(right.related, left.related))
		return false;
	return static_cast<int>(left.kind) < static_cast<int>(right.kind);
}

static bool emitDefinitionConflicts(ParseContext &context) {
	struct DefinitionPairHash {
		size_t operator()(const std::pair<PatternDefinition *, PatternDefinition *> &pair) const {
			return std::hash<PatternDefinition *>{}(pair.first) ^ (std::hash<PatternDefinition *>{}(pair.second) << 1);
		}
	};

	std::vector<PatternDefinition *> definitions;
	std::function<void(Section *)> collectDefinitions = [&](Section *section) {
		for (PatternDefinition *definition : section->patternDefinitions) {
			assert(definition && "section pattern definition list must not contain null entries");
			assert(definition->section == section && "pattern definition must point back to its owning section");
			assert(definition->resolved && "definition conflict checks require resolved pattern definitions");
			definitions.push_back(definition);
		}
		for (Section *child : section->children)
			collectDefinitions(child);
	};
	collectDefinitions(context.mainSection);
	std::sort(definitions.begin(), definitions.end(), definitionComesBefore);

	std::vector<DefinitionConflict> conflicts;
	std::unordered_set<std::pair<PatternDefinition *, PatternDefinition *>, DefinitionPairHash> seenMixedFlexPairs;
	std::unordered_set<std::pair<PatternDefinition *, PatternDefinition *>, DefinitionPairHash> seenDuplicatePairs;

	for (PatternDefinition *definition : definitions) {
		if (isFlexFunctionDefinition(definition) && definitionHasTypeConstraints(*definition))
			conflicts.push_back({DefinitionConflict::Kind::TypedFlexParameters, definition, nullptr});

		for (PatternTreeNode *endNode : definition->endNodes) {
			assert(endNode && "definition endpoint nodes must be valid");
			for (PatternDefinition *other : endNode->matchingDefinitions) {
				assert(other && "pattern tree endpoint definitions must be valid");
				assert(other->section && "pattern tree endpoint definitions must have sections");
				if (other == definition)
					continue;

				bool mixedFlexFunctionPair = definition->section->type == SectionType::Function &&
											 other->section->type == SectionType::Function &&
											 definition->section->isFlex != other->section->isFlex;
				if (mixedFlexFunctionPair) {
					PatternDefinition *nonFlexDefinition = definition->section->isFlex ? other : definition;
					PatternDefinition *flexDefinition = definition->section->isFlex ? definition : other;
					if (seenMixedFlexPairs.insert({nonFlexDefinition, flexDefinition}).second) {
						conflicts.push_back(
							{DefinitionConflict::Kind::NonFlexSpecializesFlex, nonFlexDefinition, flexDefinition}
						);
					}
					continue;
				}

				if (definitionHasTypeConstraints(*definition) || definitionHasTypeConstraints(*other))
					continue;

				PatternDefinition *earlierDefinition = definitionComesBefore(definition, other) ? definition : other;
				PatternDefinition *laterDefinition = earlierDefinition == definition ? other : definition;
				if (seenDuplicatePairs.insert({earlierDefinition, laterDefinition}).second)
					conflicts.push_back(
						{DefinitionConflict::Kind::DuplicatePatternDefinition, laterDefinition, earlierDefinition}
					);
			}
		}
	}

	if (conflicts.empty())
		return true;

	std::sort(conflicts.begin(), conflicts.end(), definitionConflictComesBefore);
	const DefinitionConflict &conflict = conflicts.front();
	switch (conflict.kind) {
	case DefinitionConflict::Kind::TypedFlexParameters:
		context.diagnostics.push_back(Diagnostic(
			context, Diagnostic::Level::Error,
			"flex parameters cannot have type constraints; convert this replacement pattern to execute:",
			conflict.primary->range
		));
		return false;
	case DefinitionConflict::Kind::NonFlexSpecializesFlex: {
		Diagnostic diagnostic(
			context, Diagnostic::Level::Error, "non-flex function cannot specialize flex function", conflict.primary->range
		);
		diagnostic.relatedInfo.push_back({"This flex function shares the same pattern endpoint:", conflict.related->range});
		context.diagnostics.push_back(std::move(diagnostic));
		return false;
	}
	case DefinitionConflict::Kind::DuplicatePatternDefinition: {
		const SyntaxConfig &syntax = syntaxConfigForRange(context, conflict.primary->range);
		Diagnostic diagnostic(context, Diagnostic::Level::Error, "duplicate pattern definition", conflict.primary->range);
		diagnostic.relatedInfo.push_back(
			{renderConfiguredMessage(syntax, "duplicate pattern definition related existing"), conflict.related->range}
		);
		appendImplicitPromotionDuplicateDetails(diagnostic, conflict.primary, conflict.related);
		context.diagnostics.push_back(std::move(diagnostic));
		return false;
	}
	}

	assert(false && "unhandled definition conflict kind");
	return false;
}

static std::vector<std::pair<std::string, Range>> collectImplicitlyPromotedParameters(const PatternDefinition *definition) {
	std::vector<std::pair<std::string, Range>> result;
	if (!definition)
		return result;
	std::function<void(const std::vector<DefinitionPatternElement> &)> visit =
		[&](const std::vector<DefinitionPatternElement> &elements) {
		for (const DefinitionPatternElement &element : elements) {
			if (element.type == PatternElement::Type::Choice) {
				for (const auto &alternative : element.alternatives)
					visit(alternative);
				continue;
			}
			if (element.type != PatternElement::Type::Variable || !element.promotedFromVariableLike ||
				!element.firstImplicitPromotionUseRange.line)
				continue;
			auto existing = std::find_if(result.begin(), result.end(), [&](const auto &entry) {
				return entry.first == element.text;
			});
			if (existing == result.end())
				result.push_back({element.text, element.firstImplicitPromotionUseRange});
		}
	};
	visit(definition->patternElements);
	return result;
}

static void
appendImplicitPromotionDuplicateDetails(Diagnostic &diagnostic, const PatternDefinition *left, const PatternDefinition *right) {
	std::vector<std::pair<std::string, Range>> leftPromoted = collectImplicitlyPromotedParameters(left);
	std::vector<std::pair<std::string, Range>> rightPromoted = collectImplicitlyPromotedParameters(right);

	auto hasName = [](const std::vector<std::pair<std::string, Range>> &entries, const std::string &name) {
		return std::any_of(entries.begin(), entries.end(), [&](const auto &entry) {
			return entry.first == name;
		});
	};
	auto appendIfDifferent = [&](const std::vector<std::pair<std::string, Range>> &source,
								 const std::vector<std::pair<std::string, Range>> &other) {
		for (const auto &[name, range] : source) {
			if (hasName(other, name))
				continue;
			diagnostic.relatedInfo.push_back({"'" + name + "' is a parameter because it was used here:", range});
		}
	};

	appendIfDifferent(leftPromoted, rightPromoted);
	appendIfDifferent(rightPromoted, leftPromoted);
}

static void appendUniqueSection(std::vector<Section *> &sections, Section *section) {
	if (!section)
		return;
	if (std::find(sections.begin(), sections.end(), section) == sections.end())
		sections.push_back(section);
}

static Range definitionNodeRange(const PatternDefinition *definition, const PatternTreeNode *node) {
	if (!definition || !node)
		return {};
	auto startIt = node->definitionStartPositions.find(const_cast<PatternDefinition *>(definition));
	if (startIt == node->definitionStartPositions.end())
		return {};
	return Range(
		definition->range.line, definition->range.start() + static_cast<int>(startIt->second),
		definition->range.start() + static_cast<int>(startIt->second + node->text.length())
	);
}

static Range definitionElementRange(const PatternDefinition *definition, const DefinitionPatternElement &element) {
	return Range(
		definition->range.line, definition->range.start() + static_cast<int>(element.startPos),
		definition->range.start() + static_cast<int>(element.startPos + element.text.length())
	);
}

struct AcceptedLiteralDiagnosticInfo {
	std::string name;
	Range range;
};

static std::vector<AcceptedLiteralDiagnosticInfo>
collectAcceptedLiteralDiagnosticInfo(const PatternMatch &match, PatternDefinition *definition) {
	std::vector<AcceptedLiteralDiagnosticInfo> result;
	if (!definition)
		return result;

	std::unordered_set<std::string> seen;
	for (const AcceptedLiteralMatch &acceptedLiteral : match.acceptedLiterals) {
		PatternTreeNode *node = acceptedLiteral.node;
		if (!node || node->type != PatternElement::Type::VariableLike)
			continue;
		Range range = definitionNodeRange(definition, node);
		if (!range.line)
			continue;
		std::string key = range.toString();
		if (!seen.insert(key).second)
			continue;
		result.push_back({node->text, range});
	}
	return result;
}

static void appendUnusedLiteralParameterNotes(ParseContext &context, PatternReference *reference, Diagnostic &diagnostic) {
	static constexpr size_t kAcceptedLiteralDiagnosticMatchStepBudget = 20000;
	MatchOptions options;
	options.acceptLiterals = true;
	options.maxSteps = kAcceptedLiteralDiagnosticMatchStepBudget;
	PatternMatch *acceptedLiteralMatch = context.match(reference, options);
	if (!acceptedLiteralMatch || !acceptedLiteralMatch->matchedEndNode) {
		delete acceptedLiteralMatch;
		return;
	}

	const SyntaxConfig &syntax = syntaxConfigForRange(context, reference->range());
	for (PatternDefinition *definition : acceptedLiteralMatch->matchedEndNode->matchingDefinitions) {
		std::vector<AcceptedLiteralDiagnosticInfo> infos =
			collectAcceptedLiteralDiagnosticInfo(*acceptedLiteralMatch, definition);
		if (infos.empty())
			continue;
		for (const AcceptedLiteralDiagnosticInfo &info : infos) {
			diagnostic.relatedInfo.push_back(
				{renderConfiguredMessage(
					 syntax, "unresolved pattern", "related unused parameter candidate", {{"parameter", info.name}}
				 ),
				 info.range}
			);
			diagnostic.quickFixes.push_back({
				renderConfiguredMessage(
					syntax, "unresolved pattern", "quick fix unused parameter candidate", {{"parameter", info.name}}
				),
				info.range,
				"{" + info.name + "}",
			});
		}
		break;
	}

	delete acceptedLiteralMatch;
}

struct AlternativePatternSuggestion {
	PatternDefinition *definition = nullptr;
	std::string spelling;
	bool isMultiWord = false;
};

static bool isParameterLikeElement(const DefinitionPatternElement &element) {
	return element.type == PatternElement::Type::Variable || canPromoteVariableLikeElement(element);
}

static bool forEachPatternSpelling(
	const std::vector<DefinitionPatternElement> &elements, size_t elementIndex, std::string &currentSpelling,
	const std::function<bool(const std::string &)> &visitor
);

static bool forEachPatternSpellingWithSuffix(
	const std::vector<DefinitionPatternElement> &alternativeElements, size_t alternativeIndex, std::string &currentSpelling,
	const std::vector<DefinitionPatternElement> &suffixElements, size_t suffixIndex,
	const std::function<bool(const std::string &)> &visitor
) {
	if (alternativeIndex >= alternativeElements.size())
		return forEachPatternSpelling(suffixElements, suffixIndex, currentSpelling, visitor);

	const DefinitionPatternElement &element = alternativeElements[alternativeIndex];
	if (element.type == PatternElement::Type::Choice) {
		for (const auto &nestedAlternative : element.alternatives) {
			if (forEachPatternSpellingWithSuffix(
					nestedAlternative, 0, currentSpelling, alternativeElements, alternativeIndex + 1, visitor
				))
				return true;
		}
		return false;
	}

	size_t previousSize = currentSpelling.size();
	currentSpelling += element.text;
	bool found = forEachPatternSpellingWithSuffix(
		alternativeElements, alternativeIndex + 1, currentSpelling, suffixElements, suffixIndex, visitor
	);
	currentSpelling.resize(previousSize);
	return found;
}

static bool forEachPatternSpelling(
	const std::vector<DefinitionPatternElement> &elements, size_t elementIndex, std::string &currentSpelling,
	const std::function<bool(const std::string &)> &visitor
) {
	if (elementIndex >= elements.size())
		return visitor(currentSpelling);

	const DefinitionPatternElement &element = elements[elementIndex];
	if (element.type == PatternElement::Type::Choice) {
		for (const auto &alternative : element.alternatives) {
			if (forEachPatternSpellingWithSuffix(alternative, 0, currentSpelling, elements, elementIndex + 1, visitor))
				return true;
		}
		return false;
	}

	size_t previousSize = currentSpelling.size();
	currentSpelling += element.text;
	bool found = forEachPatternSpelling(elements, elementIndex + 1, currentSpelling, visitor);
	currentSpelling.resize(previousSize);
	return found;
}

static bool isSingleWordPatternSpelling(const std::string &spelling) {
	std::vector<PatternElement> elements = getPatternElements(spelling);
	int wordCount = 0;
	for (const PatternElement &element : elements) {
		if (element.type == PatternElement::Type::VariableLike || element.type == PatternElement::Type::Variable)
			wordCount++;
	}
	return wordCount <= 1;
}

static bool findEnclosingParameterCandidate(PatternReference *reference, const std::string &token, Range *outRange = nullptr) {
	for (Section *sec = reference->range().section(); sec; sec = sec->parent) {
		for (PatternDefinition *def : sec->patternDefinitions) {
			bool found = false;
			Range candidateRange;
			forEachLeafElement(def->patternElements, [&](DefinitionPatternElement &element) {
				if (found || element.text != token)
					return;
				if (!isParameterLikeElement(element))
					return;
				found = true;
				if (def->range.line) {
					int start = def->range.start() + static_cast<int>(element.startPos);
					candidateRange = Range(def->range.line, start, start + static_cast<int>(element.text.size()));
				} else {
					candidateRange = def->range;
				}
			});
			if (found) {
				if (outRange)
					*outRange = candidateRange;
				return true;
			}
		}
	}
	return false;
}

static std::vector<Range> collectEnclosingParameterCandidateRanges(PatternReference *reference, const std::string &token) {
	std::vector<Range> ranges;
	for (Section *sec = reference->range().section(); sec; sec = sec->parent) {
		for (PatternDefinition *def : sec->patternDefinitions) {
			bool found = false;
			Range candidateRange;
			forEachLeafElement(def->patternElements, [&](DefinitionPatternElement &element) {
				if (found || element.text != token)
					return;
				if (!isParameterLikeElement(element))
					return;
				found = true;
				if (def->range.line) {
					int start = def->range.start() + static_cast<int>(element.startPos);
					candidateRange = Range(def->range.line, start, start + static_cast<int>(element.text.size()));
				} else {
					candidateRange = def->range;
				}
			});
			if (found && candidateRange.line)
				ranges.push_back(candidateRange);
		}
	}
	return ranges;
}

static std::vector<PatternDefinition *> collectAlternativeSearchOrder(PatternMatch *match) {
	if (!match || !match->matchedEndNode || match->matchedEndNode->matchingDefinitions.empty())
		return {};

	PatternDefinition *matchedDefinition = match->matchedEndNode->matchingDefinitions.front();
	std::vector<Section *> orderedSections;
	if (matchedDefinition && matchedDefinition->section)
		orderedSections.push_back(matchedDefinition->section);
	for (PatternDefinition *definition : match->matchedEndNode->matchingDefinitions) {
		if (!definition || !definition->section)
			continue;
		if (std::find(orderedSections.begin(), orderedSections.end(), definition->section) == orderedSections.end())
			orderedSections.push_back(definition->section);
	}

	std::vector<PatternDefinition *> orderedDefinitions;
	if (matchedDefinition)
		orderedDefinitions.push_back(matchedDefinition);
	for (Section *section : orderedSections) {
		std::vector<PatternDefinition *> sectionDefinitions = section->patternDefinitions;
		std::sort(sectionDefinitions.begin(), sectionDefinitions.end(), definitionComesBefore);
		for (PatternDefinition *definition : sectionDefinitions) {
			if (!definition)
				continue;
			if (std::find(orderedDefinitions.begin(), orderedDefinitions.end(), definition) == orderedDefinitions.end())
				orderedDefinitions.push_back(definition);
		}
	}

	return orderedDefinitions;
}

static AlternativePatternSuggestion
findAlternativePatternSuggestion(PatternReference *reference, PatternMatch *match, const std::string &originalToken) {
	for (PatternDefinition *definition : collectAlternativeSearchOrder(match)) {
		AlternativePatternSuggestion suggestion;
		std::string spelling;
		bool found =
			forEachPatternSpelling(definition->patternElements, 0, spelling, [&](const std::string &candidateSpelling) {
			if (candidateSpelling.empty() || candidateSpelling == originalToken)
				return false;

			bool isMultiWord = !isSingleWordPatternSpelling(candidateSpelling);
			if (isMultiWord) {
				suggestion = {definition, candidateSpelling, true};
				return true;
			}

			if (!findEnclosingParameterCandidate(reference, candidateSpelling)) {
				suggestion = {definition, candidateSpelling, false};
				return true;
			}
			return false;
		});
		if (found)
			return suggestion;
	}
	return {};
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
	expr->selectedPatternDefinition = (match->matchedEndNode && match->matchedEndNode->matchingDefinitions.size() == 1)
										  ? match->matchedEndNode->matchingDefinitions.front()
										  : nullptr;
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

static Range firstMatchedDefinitionRange(PatternMatch *match) {
	if (!match || !match->matchedEndNode || match->matchedEndNode->matchingDefinitions.empty())
		return {};
	return match->matchedEndNode->matchingDefinitions.front()->range;
}

static void emitExplicitDefinitionParameterAmbiguityWarnings(ParseContext &context) {
	std::function<void(Section *)> visitSection = [&](Section *section) {
		for (PatternDefinition *definition : section->patternDefinitions) {
			forEachLeafElement(definition->patternElements, [&](DefinitionPatternElement &element) {
				if (element.type != PatternElement::Type::Variable || element.typeConstraintName.empty())
					return;

				PatternDefinition *singleWordFunction = findDefinitionBySignature(context, SectionType::Function, element.text);
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
static void removeVariableReferencesFromMatch(
	ParseContext &context, PatternReference *reference, PatternMatch &match, std::vector<Section *> &affectedSections,
	bool revertImplicitPromotions
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
				}

				// Revert Variable→VariableLike in pattern definitions and mark for re-resolution.
				// Must remove from tree BEFORE changing element types (tree was built with old types).
				for (PatternDefinition *def : ownerSection->patternDefinitions) {
					bool needsReResolution = false;
					forEachLeafElement(def->patternElements, [&](DefinitionPatternElement &element) {
						// Only revert unconstrained Variables — typed arguments ({type:name})
						// were never VariableLike and must not be reverted.
						if (element.type == PatternElement::Type::Variable && element.text == name &&
							element.typeConstraintName.empty() && element.promotedFromVariableLike)
							needsReResolution = true;
					});
					if (needsReResolution && def->resolved) {
						// Remove from tree while elements still reflect the old path.
						SectionType treeType =
							ownerSection->type == SectionType::Class ? SectionType::Function : ownerSection->type;
						context.patternTrees[(size_t)treeType]->removePatternPart(def->patternElements, def);
					}
					// Now revert element types (only unconstrained — typed arguments stay Variable).
					forEachLeafElement(def->patternElements, [&](DefinitionPatternElement &element) {
						if (element.type == PatternElement::Type::Variable && element.text == name &&
							element.typeConstraintName.empty() && element.promotedFromVariableLike) {
							element.type = PatternElement::Type::VariableLike;
							element.promotedFromVariableLike = false;
							element.firstImplicitPromotionUseRange = {};
							def->resolved = false;
							ownerSection->patternDefinitionsResolved = false;
							appendUniqueSection(affectedSections, ownerSection);
						}
					});
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
	if (!reference->resolved || !reference->match)
		return affectedSections;

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
	if (!section)
		return false;

	bool changed = false;
	std::vector<std::pair<std::string, VariableReference *>> ownedDefinitions;
	ownedDefinitions.reserve(section->variableDefinitions.size());
	for (const auto &[name, definitionReference] : section->variableDefinitions)
		ownedDefinitions.emplace_back(name, definitionReference);

	for (const auto &[name, definitionReference] : ownedDefinitions) {
		if (!definitionReference)
			continue;
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
		}

		for (PatternDefinition *def : section->patternDefinitions) {
			bool needsReResolution = false;
			forEachLeafElement(def->patternElements, [&](DefinitionPatternElement &element) {
				if (element.type == PatternElement::Type::Variable && element.text == name &&
					element.typeConstraintName.empty() && element.promotedFromVariableLike)
					needsReResolution = true;
			});
			if (!needsReResolution)
				continue;
			if (def->resolved) {
				SectionType treeType = section->type == SectionType::Class ? SectionType::Function : section->type;
				context.patternTrees[(size_t)treeType]->removePatternPart(def->patternElements, def);
			}
			forEachLeafElement(def->patternElements, [&](DefinitionPatternElement &element) {
				if (element.type == PatternElement::Type::Variable && element.text == name &&
					element.typeConstraintName.empty() && element.promotedFromVariableLike) {
					element.type = PatternElement::Type::VariableLike;
					element.promotedFromVariableLike = false;
					element.firstImplicitPromotionUseRange = {};
					def->resolved = false;
					section->patternDefinitionsResolved = false;
					changed = true;
				}
			});
		}
	}

	return changed;
}

// Resolve a list of pattern references against the tree. Returns true if all resolved.
static bool resolveReferences(
	ParseContext &context, std::list<PatternReference *> &references, bool decrementCounts,
	std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> *defToRefs = nullptr, const char *phase = "body"
) {
	return std::erase_if(references, [&context, decrementCounts, defToRefs, phase](PatternReference *reference) {
		PatternMatch *match = context.match(reference);
		if (match) {
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
					visitPatternNameWithFoundState(def->patternElements, varName, false, [&](DefinitionPatternElement &elem) {
						if (elem.type != PatternElement::Type::VariableLike || !canPromoteVariableLikeElement(elem))
							return false;
						if (elem.text == varName) {
							elem.type = PatternElement::Type::Variable;
							return true;
						}
						return false;
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
			std::vector<DefinitionPatternElement> parsedElements;
			if (!parsePatternElements(
					context, unresolvedDefinition->range, unresolvedDefinition->range.subString, parsedElements
				)) {
				hadPatternParseError = true;
				continue;
			}
			unresolvedDefinition->patternElements = std::move(parsedElements);
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
	std::vector<Section *> pendingPromotionCleanupSections;

	// Helper: after adding a definition to the tree, find less-specific definitions
	// and unresolve any references that matched them so they can re-match the more specific one.
	auto invalidateStaleMatches = [&](PatternDefinition *definition, SectionType treeType) {
		auto lessSpecific = context.patternTrees[(size_t)treeType]->findLessSpecificDefinitions(definition->patternElements);
		std::sort(lessSpecific.begin(), lessSpecific.end(), definitionComesBefore);
		traceResolution(
			"invalidate base=" + definitionTraceId(definition) + " candidates=" + std::to_string(lessSpecific.size())
		);
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
				auto affectedSections = unresolveReference(context, ref, definitionToReferences, false);
				std::sort(affectedSections.begin(), affectedSections.end(), sectionComesBefore);
				bodyReferences.push_back(ref);
				for (Section *sec : affectedSections)
					appendUniqueSection(pendingPromotionCleanupSections, sec);
			}
		}
	};

	// Helper: add a definition to the pattern tree.
	auto addDefinitionToTree = [&](PatternDefinition *definition, SectionType treeType) {
		context.patternTrees[(size_t)treeType]->addPatternPart(definition->patternElements, definition);
		traceResolution("add " + definitionTraceId(definition) + " tree=" + std::to_string((int)treeType));
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
					forEachLeafElement(definition->patternElements, [&](DefinitionPatternElement &element) {
						if (element.type == PatternElement::Type::VariableLike) {
							if (definition->patternElements.size() > 1) {
								if (section->variableLikeCounts[element.text] != 0) {
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

		if (unResolvedSections.size() < sectionsBefore) {
			madeProgress = true;
		}
		if (staleInvalidationOccurred) {
			madeProgress = true;
		}

		if (resolveReferences(context, bodyReferences, true, &definitionToReferences, "body")) {
			madeProgress = true;
		}
		if (!pendingPromotionCleanupSections.empty()) {
			std::sort(pendingPromotionCleanupSections.begin(), pendingPromotionCleanupSections.end(), sectionComesBefore);
			bool cleanupChanged = false;
			for (Section *sec : pendingPromotionCleanupSections) {
				if (!cleanupStaleImplicitPromotionsInSection(context, sec))
					continue;
				cleanupChanged = true;
				appendUniqueSection(pendingRequeueSections, sec);
			}
			pendingPromotionCleanupSections.clear();
			if (cleanupChanged)
				madeProgress = true;
		}

		// Resolve type constraints on definition elements ({type:name} syntax).
		// Resolve capture constraints through the normal type-expression parser so
		// compound constraints like "4 float array" work the same as declared types.
		{
			std::function<void(Section *)> resolveTypeConstraints = [&](Section *section) {
				for (PatternDefinition *def : section->patternDefinitions) {
					for (auto &elem : def->patternElements) {
						if (elem.typeConstraintName.empty() || elem.resolvedTypeConstraint.isDeduced())
							continue;

						int constraintEnd = def->range.start() + static_cast<int>(elem.startPos) - 1;
						int constraintStart = constraintEnd - static_cast<int>(elem.typeConstraintName.size());
						Range constraintRange(def->range.line, constraintStart, constraintEnd);
						DataType typeRef;
						if (resolveTypeConstraintExpression(
								context, def->section, constraintRange, elem.typeConstraintName, typeRef
							))
							elem.resolvedTypeConstraint = typeRef.toReferencedType();
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
			bool promotedCyclicParameter = false;
			for (Section *section : unResolvedSections) {
				std::unordered_set<std::string> promotableNames;
				std::unordered_set<Section *> visitedSections;
				collectPromotablePatternNames(section, promotableNames, visitedSections);
				if (promotableNames.empty())
					continue;
				for (PatternReference *reference : bodyReferences) {
					if (!reference)
						continue;
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
		for (PatternReference *reference : bodyReferences) {
			Diagnostic diagnostic(context, Diagnostic::Level::Error, "unresolved pattern", reference->range());
			appendUnusedLiteralParameterNotes(context, reference, diagnostic);
			context.diagnostics.push_back(std::move(diagnostic));
		}
		for (PatternReference *reference : globalReferences) {
			Diagnostic diagnostic(context, Diagnostic::Level::Error, "unresolved pattern", reference->range());
			appendUnusedLiteralParameterNotes(context, reference, diagnostic);
			context.diagnostics.push_back(std::move(diagnostic));
		}
	};

	// Phase 2 depends on fully-resolved definitions. If phase 1 hit max iterations
	// and left unresolved sections, report them directly.
	if (!unResolvedSections.empty()) {
		emitUnresolvedPatternDiagnostics();
		return false;
	}

	// Phase 2: resolve global references (all definitions are now in the tree)
	if (!emitDefinitionConflicts(context))
		return false;

	emitDuplicatePatternWordWarnings(context);

	for (int resolutionIteration = 0; resolutionIteration < context.options.maxResolutionIterations; resolutionIteration++) {
		resolveReferences(context, globalReferences, false, nullptr, "global");
		if (globalReferences.empty())
			break;
	}

	if (!bodyReferences.empty() || !globalReferences.empty()) {
		emitUnresolvedPatternDiagnostics();
		return false;
	}

	// Validate type constraints — report unresolved ones as errors
	{
		std::function<void(Section *)> validateTypeConstraints = [&](Section *section) {
			for (PatternDefinition *def : section->patternDefinitions) {
				for (auto &elem : def->patternElements) {
					if (!elem.typeConstraintName.empty() && !elem.resolvedTypeConstraint.isDeduced()) {
						context.diagnostics.push_back(Diagnostic(
							context, Diagnostic::Level::Error, "unknown type constraint", def->range, "type_constraint",
							elem.typeConstraintName
						));
					}
				}
			}
			for (Section *child : section->children)
				validateTypeConstraints(child);
		};
		validateTypeConstraints(context.mainSection);
	}

	emitExplicitDefinitionParameterAmbiguityWarnings(context);

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
							context.diagnostics.push_back(Diagnostic(
								context, Diagnostic::Level::Error, "precedence target not found", def->range, "target",
								beforeStr
							));
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
							context.diagnostics.push_back(Diagnostic(
								context, Diagnostic::Level::Error, "precedence target not found", def->range, "target", afterStr
							));
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
					Diagnostic(context, Diagnostic::Level::Error, "precedence cycle detected", Range())
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
			highestSection->variables[name] = new Variable(name, definition, groupIsGlobal);
			for (VariableReference *ref : groupRefs) {
				if (ref != definition)
					ref->definition = definition;
			}
		}
	}
	return true;
}
