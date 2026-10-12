#pragma once
#include "section.h"
#include <optional>

// A block such as `in this file, by default:`. Its lines state rules for the enclosing file, class or definition and
// are never executed.
struct ScopeRulesSection : public Section {
	enum class Scope { File, Class, Function };

	ScopeRulesSection(Section *parent, ScopeRules &rules, Scope scope, bool absolute)
		: Section(SectionType::ScopeRules, parent), rules(rules), scope(scope), absolute(absolute) {}

	ScopeRules &rules;
	Scope scope;
	bool absolute;

	bool processLine(ParseContext &context, CodeLine *line) override;
	Section *createSection(ParseContext &context, CodeLine *line) override;
	bool runsInEnclosingBody() const override { return false; }
};

// Creates the rule block opened by `line` inside `owner`, or returns nullopt when the line is no scope rule header.
// A header in the wrong place reports an error and yields nullptr.
std::optional<Section *> createScopeRulesSection(ParseContext &context, CodeLine *line, Section *owner);

// Applies the locality rules in effect at `line` to a definition section created inside `owner`, validating the
// explicit `local` and `public` modifiers.
bool applyDefinitionLocality(
	ParseContext &context, CodeLine *line, Section *owner, Section *definition, std::optional<Range> localModifier,
	std::optional<Range> publicModifier
);
