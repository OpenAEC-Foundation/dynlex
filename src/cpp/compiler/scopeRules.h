#pragma once
#include "range.h"
#include "typeConstraint.h"
#include <array>
#include <cstddef>
#include <optional>

// The definition kinds whose locality a scope rule can decide.
enum class LocalDefinitionKind { Function, Class, Section, Conversion, Count };

// Where a scope rule was stated. Rules from a block without the default marker are absolute: explicit code that
// contradicts them still wins, but is reported.
struct ScopeRuleSource {
	Range range;
	bool absolute = false;
};

struct NumberScopeRule {
	ScopeRuleSource source;
	TypeConstraint properties = TypeConstraint::any();
};

// The rules stated for one scope: a file, a class or a function, conversion or section definition.
struct ScopeRules {
	std::array<std::optional<ScopeRuleSource>, static_cast<size_t>(LocalDefinitionKind::Count)> local;
	std::optional<NumberScopeRule> numbers;
	// A number rule stated in this scope whose properties are not inferred yet.
	bool numberRulePending = false;
	bool hasAbsoluteBlock = false;
	bool hasDefaultBlock = false;
};
