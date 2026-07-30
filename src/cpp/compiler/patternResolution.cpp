#include "classSection.h"
#include "compiler.h"
#include "compilerUtils.h"
#include "expression.h"
#include "functionSection.h"
#include "intrinsicInfo.h"
#include "patternElement.h"
#include "patternTreeNode.h"
#include "replacementSection.h"
#include "transformedPattern.h"
#include "type.h"
#include "variable.h"
#include <algorithm>
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
enum class ClassPropertyAccessorSyntax {
	NonPossessive,
	SingularPossessive,
	PluralPossessive,
};

static CodeLine *createGeneratedLine(
	ParseContext &context, const Range &sourceRange, Section *section, std::string text, int logicalLineOffset = 0
) {
	auto generatedLine = std::make_unique<CodeLine>(std::string_view{}, sourceRange.line->sourceFile);
	generatedLine->setOwnedText(std::move(text));
	generatedLine->rightTrimmedText = generatedLine->fullText;
	generatedLine->patternText = generatedLine->fullText;
	generatedLine->sourceFileLineIndex = sourceRange.line->sourceFileLineIndex;
	generatedLine->mergedLineIndex = sourceRange.line->mergedLineIndex + logicalLineOffset;
	generatedLine->section = section;
	SourceLocation sourceStart = sourceRange.sourceStart();
	generatedLine->sourceSlices.push_back({0, 0, sourceStart.sourceFile, sourceStart.sourceFileLineIndex, sourceStart.column});
	CodeLine *result = generatedLine.get();
	context.ownedCodeLines.push_back(std::move(generatedLine));
	return result;
}

static PatternDefinition *createClassPropertyPatternDefinition(
	ParseContext &context, FunctionSection *accessorSection, ClassDefinition *classDefinition, const FieldDefinition &field,
	ClassPropertyAccessorSyntax syntax
) {
	std::string patternText;
	switch (syntax) {
	case ClassPropertyAccessorSyntax::NonPossessive:
		patternText = "the " + field.name + " of instance";
		break;
	case ClassPropertyAccessorSyntax::SingularPossessive:
		patternText = "instance's " + field.name;
		break;
	case ClassPropertyAccessorSyntax::PluralPossessive:
		patternText = "instance' " + field.name;
		break;
	}
	CodeLine *patternLine = createGeneratedLine(context, field.range, accessorSection, patternText);
	auto *definition = new PatternDefinition(Range(patternLine, patternLine->fullText), accessorSection);
	definition->hasPrebuiltPatternElements = true;
	definition->isGeneratedClassPropertyAccessor = true;

	auto addLiteralSequence = [&](std::string_view text, size_t startPos) {
		for (const PatternElement &element : getPatternElements(text)) {
			DefinitionPatternElement literal(element);
			literal.startPos += startPos;
			definition->patternElements.push_back(std::move(literal));
		}
	};
	auto addInstance = [&](size_t startPos) {
		DefinitionPatternElement instance(PatternElement::Type::Variable, "instance", startPos);
		DataType ownerType{DataType::Kind::Class};
		ownerType.classDefinition = classDefinition;
		instance.resolvedTypeConstraint = TypeConstraint::any();
		instance.resolvedTypeConstraint.kind = DataType::Kind::Class;
		instance.resolvedTypeConstraint.constrainsClassDefinition = true;
		instance.resolvedTypeConstraint.classDefinition = classDefinition;
		instance.resolvedParameterType = ownerType;
		definition->patternElements.push_back(std::move(instance));
	};

	if (syntax != ClassPropertyAccessorSyntax::NonPossessive) {
		addInstance(0);
		addLiteralSequence(patternText.substr(8), 8);
	} else {
		const size_t instanceStart = patternText.size() - std::string_view("instance").size();
		addLiteralSequence(std::string_view(patternText).substr(0, instanceStart), 0);
		addInstance(instanceStart);
	}

	accessorSection->patternDefinitions.push_back(definition);
	return definition;
}

static void generateClassPropertyPatterns(ParseContext &context) {
	std::vector<ClassSection *> classSections;
	std::function<void(Section *)> collectClasses = [&](Section *section) {
		if (section->type == SectionType::Class)
			classSections.push_back(static_cast<ClassSection *>(section));
		for (Section *child : section->children)
			collectClasses(child);
	};
	collectClasses(context.mainSection);

	for (ClassSection *classSection : classSections) {
		ClassDefinition *classDefinition = classSection->classDefinition;
		for (const FieldDefinition &field : classDefinition->fields) {
			auto *accessorSection = new FunctionSection(context.mainSection);
			accessorSection->isFlex = true;
			accessorSection->isLocal = classSection->isLocal;
			createClassPropertyPatternDefinition(
				context, accessorSection, classDefinition, field, ClassPropertyAccessorSyntax::NonPossessive
			);
			createClassPropertyPatternDefinition(
				context, accessorSection, classDefinition, field, ClassPropertyAccessorSyntax::SingularPossessive
			);
			createClassPropertyPatternDefinition(
				context, accessorSection, classDefinition, field, ClassPropertyAccessorSyntax::PluralPossessive
			);
			for (PatternDefinition *definition : accessorSection->patternDefinitions)
				accessorSection->indexExplicitParameters(*definition);

			auto *replacementSection = new ReplacementSection(accessorSection);
			accessorSection->executionSection = replacementSection;
			CodeLine *bodyLine = createGeneratedLine(
				context, field.range, replacementSection, "@intrinsic(\"property\", instance, \"" + field.name + "\")", 1
			);
			replacementSection->codeLines.push_back(bodyLine);

			auto *intrinsic = new Expression();
			intrinsic->kind = Expression::Kind::IntrinsicCall;
			intrinsic->intrinsicName = "property";
			intrinsic->range = Range(bodyLine, bodyLine->fullText);

			auto *intrinsicName = new Expression();
			intrinsicName->kind = Expression::Kind::Literal;
			intrinsicName->literalValue = std::string("property");
			intrinsicName->range = intrinsic->range;
			intrinsic->arguments.push_back(intrinsicName);

			auto *instance = new Expression();
			instance->kind = Expression::Kind::Variable;
			instance->range = intrinsic->range;
			instance->variable = context.createVariableReference(instance->range, "instance");
			intrinsic->arguments.push_back(instance);
			replacementSection->addVariableReference(context, instance->variable);

			auto *propertyName = new Expression();
			propertyName->kind = Expression::Kind::Literal;
			propertyName->literalValue = field.name;
			propertyName->range = intrinsic->range;
			intrinsic->arguments.push_back(propertyName);

			bodyLine->expression = intrinsic;
			bodyLine->resolved = true;
		}
	}
}

static void populateClassPatternNames(Section *section) {
	requireCompilerInvariant(section && section->type == SectionType::Class, "class pattern names require a class section");
	auto *classSection = static_cast<ClassSection *>(section);
	requireCompilerInvariant(classSection->classDefinition, "class section is missing its definition");

	std::vector<std::string> names;
	for (PatternDefinition *definition : classSection->patternDefinitions) {
		requireCompilerInvariant(definition, "class section contains a null pattern definition");
		for (std::string spelling : canonicalPatternSpellings(definition->patternElements)) {
			if (std::find(names.begin(), names.end(), spelling) == names.end())
				names.push_back(std::move(spelling));
		}
	}
	requireCompilerInvariant(!names.empty(), "resolved class has no pattern names");
	std::ranges::sort(names, [](const std::string &left, const std::string &right) {
		if (left.size() != right.size())
			return left.size() < right.size();
		return left < right;
	});
	classSection->classDefinition->patternNames = std::move(names);
}

static bool sectionComesBefore(const Section *left, const Section *right) {
	auto locationKey = [](const Section *section) {
		if (!section)
			return std::tuple{INT_MAX, INT_MAX, INT_MAX};
		int mergedLineIndex = section->openingLine ? section->openingLine->mergedLineIndex : INT_MAX - 1;
		int sourceLineIndex = section->openingLine ? section->openingLine->sourceFileLineIndex : INT_MAX - 1;
		return std::tuple{mergedLineIndex, sourceLineIndex, static_cast<int>(section->type)};
	};
	auto leftLocation = locationKey(left);
	auto rightLocation = locationKey(right);
	if (leftLocation != rightLocation)
		return leftLocation < rightLocation;
	auto text = [](const Section *section) {
		if (!section)
			return std::string{};
		if (section->openingLine)
			return std::string(section->openingLine->patternText);
		return section->toString();
	};
	return text(left) < text(right);
}

static bool referenceComesBefore(const PatternReference *left, const PatternReference *right) {
	auto locationKey = [](const PatternReference *reference) {
		if (!reference)
			return std::tuple{INT_MAX, INT_MAX, INT_MAX};
		const Range &range = reference->range();
		int lineIndex = range.line ? range.line->mergedLineIndex : INT_MAX - 1;
		return std::tuple{lineIndex, range.start(), range.end()};
	};
	auto leftLocation = locationKey(left);
	auto rightLocation = locationKey(right);
	if (leftLocation != rightLocation)
		return leftLocation < rightLocation;
	std::string leftText = left ? left->pattern.text : "";
	std::string rightText = right ? right->pattern.text : "";
	return leftText < rightText;
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
			if (section->canPromoteImplicitParameter(*definition, element))
				names.insert(element.text);
		});
	}
	for (Section *child : section->children)
		collectPromotablePatternNames(child, names, visited);
}

static bool
promotePatternNameInSectionChain(ParseContext &context, Section *section, const std::string &name, const Range &useRange) {
	for (Section *current = section; current; current = current->parent) {
		if (current->resolvePatternParameterBinding(context, name, useRange))
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

struct PatternDomainAutomaton {
	struct Transition {
		size_t target;
		PatternElement element;
		TypeConstraint constraint = TypeConstraint::any();
		int constraintSpecificity = 0;
	};
	struct State {
		std::vector<size_t> epsilonTargets;
		std::vector<Transition> transitions;
		bool accepting = false;
	};

	std::vector<State> states{2};

	explicit PatternDomainAutomaton(const PatternDefinition &definition) {
		states[1].accepting = true;
		requireCompilerInvariant(
			definition.indexedPaths.size() == definition.signaturePaths.size(),
			"pattern paths and compiled signatures diverged during conflict validation"
		);
		for (size_t pathIndex = 0; pathIndex < definition.indexedPaths.size(); pathIndex++)
			addSequence(definition.indexedPaths[pathIndex], definition.signaturePaths[pathIndex], 0, 1);
	}

  private:
	size_t addState() {
		states.emplace_back();
		return states.size() - 1;
	}

	void
	addSequence(const std::vector<PatternElement> &elements, const PatternPathSignature &signature, size_t start, size_t end) {
		if (elements.empty()) {
			states[start].epsilonTargets.push_back(end);
			return;
		}
		size_t current = start;
		size_t parameterIndex = 0;
		for (size_t i = 0; i < elements.size(); i++) {
			size_t next = i + 1 == elements.size() ? end : addState();
			Transition transition{next, elements[i]};
			if (elements[i].type == PatternElement::Type::Variable || elements[i].type == PatternElement::Type::Word) {
				requireCompilerInvariant(
					parameterIndex < signature.parameters.size(),
					"compiled signature has fewer parameters than its pattern path"
				);
				const TypeConstraintTemplate &constraint = signature.parameters[parameterIndex++].constraint;
				transition.constraint = constraint.structuralEnvelope();
				transition.constraintSpecificity = constraint.structuralSpecificity();
			}
			states[current].transitions.push_back(std::move(transition));
			current = next;
		}
		requireCompilerInvariant(
			parameterIndex == signature.parameters.size(), "compiled signature has more parameters than its pattern path"
		);
	}
};

struct PatternDomainSearchState {
	size_t leftState;
	size_t rightState;
	int constraintScoreDifference;

	bool operator==(const PatternDomainSearchState &) const = default;
};

struct PatternDomainSearchStateHash {
	size_t operator()(const PatternDomainSearchState &state) const {
		size_t hash = std::hash<size_t>{}(state.leftState);
		hash ^= std::hash<size_t>{}(state.rightState) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<int>{}(state.constraintScoreDifference) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		return hash;
	}
};

static bool
patternTransitionsOverlap(const PatternDomainAutomaton::Transition &left, const PatternDomainAutomaton::Transition &right) {
	if (left.element.type != right.element.type)
		return false;
	if (left.element.type == PatternElement::Type::Variable)
		return left.constraint.structurallyOverlaps(right.constraint);
	if (left.element.type == PatternElement::Type::Word)
		return true;
	return left.element.text == right.element.text;
}

static bool
definitionsHaveAmbiguousTypeDomainOverlap(const PatternDefinition &leftDefinition, const PatternDefinition &rightDefinition) {
	PatternDomainAutomaton left(leftDefinition);
	PatternDomainAutomaton right(rightDefinition);
	std::vector<PatternDomainSearchState> pending{{0, 0, 0}};
	std::unordered_set<PatternDomainSearchState, PatternDomainSearchStateHash> visited;

	while (!pending.empty()) {
		PatternDomainSearchState current = pending.back();
		pending.pop_back();
		if (!visited.insert(current).second)
			continue;

		const auto &leftState = left.states[current.leftState];
		const auto &rightState = right.states[current.rightState];
		if (leftState.accepting && rightState.accepting && current.constraintScoreDifference == 0)
			return true;

		for (size_t target : leftState.epsilonTargets)
			pending.push_back({target, current.rightState, current.constraintScoreDifference});
		for (size_t target : rightState.epsilonTargets)
			pending.push_back({current.leftState, target, current.constraintScoreDifference});
		for (const auto &leftTransition : leftState.transitions) {
			for (const auto &rightTransition : rightState.transitions) {
				if (!patternTransitionsOverlap(leftTransition, rightTransition))
					continue;
				int nextDifference = current.constraintScoreDifference;
				if (leftTransition.element.type == PatternElement::Type::Variable ||
					leftTransition.element.type == PatternElement::Type::Word) {
					nextDifference += leftTransition.constraintSpecificity;
					nextDifference -= rightTransition.constraintSpecificity;
				}
				pending.push_back({leftTransition.target, rightTransition.target, nextDifference});
			}
		}
	}

	return false;
}

static void
appendImplicitPromotionDuplicateDetails(Diagnostic &diagnostic, const PatternDefinition *left, const PatternDefinition *right);

struct DefinitionConflict {
	PatternDefinition *primary;
	PatternDefinition *related{};
};

static bool definitionConflictComesBefore(const DefinitionConflict &left, const DefinitionConflict &right) {
	if (patternDefinitionComesBefore(left.primary, right.primary))
		return true;
	if (patternDefinitionComesBefore(right.primary, left.primary))
		return false;
	if (patternDefinitionComesBefore(left.related, right.related))
		return true;
	if (patternDefinitionComesBefore(right.related, left.related))
		return false;
	return false;
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
			requireCompilerInvariant(definition != nullptr, "section pattern definition list must not contain null entries");
			requireCompilerInvariant(
				definition->section == section, "pattern definition must point back to its owning section"
			);
			requireCompilerInvariant(definition->resolved, "definition conflict checks require resolved pattern definitions");
			definitions.push_back(definition);
		}
		for (Section *child : section->children)
			collectDefinitions(child);
	};
	collectDefinitions(context.mainSection);
	std::sort(definitions.begin(), definitions.end(), patternDefinitionComesBefore);

	std::vector<DefinitionConflict> conflicts;
	std::unordered_set<std::pair<PatternDefinition *, PatternDefinition *>, DefinitionPairHash> seenDuplicatePairs;

	for (PatternDefinition *definition : definitions) {
		for (PatternTreeNode *endNode : definition->endNodes) {
			requireCompilerInvariant(endNode != nullptr, "definition endpoint nodes must be valid");
			for (PatternDefinition *other : endNode->matchingDefinitions) {
				requireCompilerInvariant(other != nullptr, "pattern tree endpoint definitions must be valid");
				requireCompilerInvariant(other->section != nullptr, "pattern tree endpoint definitions must have sections");
				if (other == definition)
					continue;
				if (definition->section->isConversion && other->section->isConversion)
					continue;
				if (!patternDefinitionsShareVisibilityScope(*definition, *other))
					continue;

				PatternDefinition *earlierDefinition = patternDefinitionComesBefore(definition, other) ? definition : other;
				PatternDefinition *laterDefinition = earlierDefinition == definition ? other : definition;
				if (!seenDuplicatePairs.insert({earlierDefinition, laterDefinition}).second)
					continue;
				bool flexSectionOverload = earlierDefinition->section->type == SectionType::Section &&
										   laterDefinition->section->type == SectionType::Section &&
										   (earlierDefinition->section->isFlex || laterDefinition->section->isFlex);
				if (flexSectionOverload || definitionsHaveAmbiguousTypeDomainOverlap(*earlierDefinition, *laterDefinition))
					conflicts.push_back({laterDefinition, earlierDefinition});
			}
		}
	}

	if (conflicts.empty())
		return true;

	std::sort(conflicts.begin(), conflicts.end(), definitionConflictComesBefore);
	const DefinitionConflict &conflict = conflicts.front();
	const SyntaxConfig &syntax = syntaxConfigForRange(context, conflict.primary->range);
	bool flexSectionOverload = conflict.primary->section->type == SectionType::Section &&
							   conflict.related->section->type == SectionType::Section &&
							   (conflict.primary->section->isFlex || conflict.related->section->isFlex);
	Diagnostic diagnostic(
		context, Diagnostic::Level::Error,
		flexSectionOverload ? "flex sections cannot be overloaded" : "duplicate pattern definition", conflict.primary->range
	);
	diagnostic.relatedInfo.push_back({
		flexSectionOverload ? "conflicts with this flex section"
							: renderConfiguredMessage(syntax, "duplicate pattern definition related existing"),
		conflict.related->range,
	});
	appendImplicitPromotionDuplicateDetails(diagnostic, conflict.primary, conflict.related);
	context.diagnostics.push_back(std::move(diagnostic));
	return false;
}

static std::vector<std::pair<std::string, Range>> collectImplicitlyPromotedParameters(const PatternDefinition *definition) {
	std::vector<std::pair<std::string, Range>> result;
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
	if (std::find(sections.begin(), sections.end(), section) == sections.end())
		sections.push_back(section);
}

static void eraseOwnedSectionVariable(Section *section, const std::string &name, const VariableReference *definitionReference) {
	auto variableIt = section->variables.find(name);
	if (variableIt == section->variables.end() || !variableIt->second || variableIt->second->definition != definitionReference)
		return;
	Variable *variable = variableIt->second;
	section->variables.erase(variableIt);
	delete variable;
}

static std::vector<Range> definitionNodeRanges(
	const PatternDefinition *definition, const PatternTreeNode *node, const std::vector<PatternTreeNode *> &matchedNodePath
) {
	std::vector<Range> ranges;
	std::unordered_set<size_t> seenStartPositions;
	for (size_t pathIndex : matchingPatternPathIndices(matchedNodePath, definition)) {
		const auto &nodes = definition->indexedNodePaths[pathIndex];
		const auto &elements = definition->indexedPaths[pathIndex];
		for (size_t elementIndex = 0; elementIndex < nodes.size(); elementIndex++) {
			if (nodes[elementIndex] != node)
				continue;
			size_t startPos = elements[elementIndex].startPos;
			if (!seenStartPositions.insert(startPos).second)
				break;
			ranges.emplace_back(
				definition->range.line, definition->range.start() + static_cast<int>(startPos),
				definition->range.start() + static_cast<int>(startPos + node->text.length())
			);
			break;
		}
	}
	requireCompilerInvariant(!ranges.empty(), "matched pattern node has no occurrence on its selected path");
	return ranges;
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
	std::unordered_set<std::string> seen;
	for (const AcceptedLiteralMatch &acceptedLiteral : match.acceptedLiterals) {
		PatternTreeNode *node = acceptedLiteral.node;
		if (node->type != PatternElement::Type::VariableLike)
			continue;
		for (Range range : definitionNodeRanges(definition, node, match.nodesPassed)) {
			std::string key = range.toString();
			if (!seen.insert(key).second)
				continue;
			result.push_back({node->text, range});
		}
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
	for (PatternDefinition *definition : acceptedLiteralMatch->matchingDefinitions) {
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
		std::vector<Range> candidateRanges = sec->patternParameterCandidateRanges(token);
		if (!candidateRanges.empty()) {
			if (outRange)
				*outRange = candidateRanges.front();
			return true;
		}
	}
	return false;
}

static std::vector<Range> collectEnclosingParameterCandidateRanges(PatternReference *reference, const std::string &token) {
	std::vector<Range> ranges;
	for (Section *sec = reference->range().section(); sec; sec = sec->parent) {
		std::vector<Range> sectionRanges = sec->patternParameterCandidateRanges(token);
		ranges.insert(ranges.end(), sectionRanges.begin(), sectionRanges.end());
	}
	return ranges;
}

static std::vector<PatternDefinition *> collectAlternativeSearchOrder(PatternMatch *match) {
	if (!match || match->matchingDefinitions.empty())
		return {};

	PatternDefinition *matchedDefinition = match->matchingDefinitions.front();
	std::vector<Section *> orderedSections;
	if (matchedDefinition && matchedDefinition->section)
		orderedSections.push_back(matchedDefinition->section);
	for (PatternDefinition *definition : match->matchingDefinitions) {
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
		std::sort(sectionDefinitions.begin(), sectionDefinitions.end(), patternDefinitionComesBefore);
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
		for (const std::string &candidateSpelling : canonicalPatternSpellings(definition->patternElements)) {
			if (candidateSpelling.empty() || candidateSpelling == originalToken)
				continue;

			bool isMultiWord = !isSingleWordPatternSpelling(candidateSpelling);
			if (isMultiWord)
				return {definition, candidateSpelling, true};

			if (!findEnclosingParameterCandidate(reference, candidateSpelling))
				return {definition, candidateSpelling, false};
		}
	}
	return {};
}
} // namespace

#include "patternResolutionExpansion.inl"
#include "patternResolutionMatching.inl"
