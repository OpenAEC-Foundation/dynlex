#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

struct ParseContext;
struct Section;
struct VariableReference;

struct VariableBindingUnit {
	std::string name;
	Section *domain;
	bool global;
	std::vector<VariableReference *> references;
};

struct VariableBindingGroup {
	Section *owner;
	VariableReference *definition;
	std::vector<VariableReference *> references;
};

struct VariableBindingWork {
	size_t evaluated = 0, reused = 0, referencesClassified = 0, referencesRebound = 0, groupsMaterialized = 0;
	double milliseconds = 0;
};

struct VariableBindingHooks {
	using Groups = std::vector<VariableBindingGroup>;
	std::function<Groups(const VariableBindingUnit &, const std::function<Groups()> &)> unit;
	std::function<void(Section *)> complete;
	VariableBindingWork &work;
};

void materializeVariableGroups(ParseContext &context, Section *domain = nullptr);
