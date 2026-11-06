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

// Apply locality after parsing so declarations also see rules written later in their named scope.
bool applyDefinitionLocalities(ParseContext &context);
