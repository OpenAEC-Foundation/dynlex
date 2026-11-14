#include "variableBindings.h"
#include "parseContext.h"
#include "section/variable.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <unordered_map>

namespace {
static std::pair<int, int> variableReferenceSourcePosition(const VariableReference *reference) {
	requireCompilerInvariant(reference && reference->range.line, "variable reference has no source position");
	return {reference->range.line->mergedLineIndex, reference->range.start()};
}

static bool isVariableFunctionScope(const Section *section) {
	return (section->type == SectionType::Function && !section->isFlex) || section->type == SectionType::Retain ||
		   section->type == SectionType::Release;
}

static Section *enclosingVariableFunctionScope(Section *section) {
	for (Section *ancestor = section; ancestor; ancestor = ancestor->parent) {
		if (isVariableFunctionScope(ancestor))
			return ancestor;
	}
	return nullptr;
}

static bool functionDeclaresGlobalName(const Section *functionScope, const std::string &name) {
	return functionScope && std::ranges::find(functionScope->globalVariables, name) != functionScope->globalVariables.end();
}

static Section *visibleExplicitVariableSection(Section *section, const std::string &name, const VariableReference *reference) {
	Section *functionScope = enclosingVariableFunctionScope(section);
	bool declaredGlobalHere = functionDeclaresGlobalName(functionScope, name);
	const std::pair<int, int> referencePosition = variableReferenceSourcePosition(reference);
	for (Section *current = section; current; current = current->parent) {
		auto declaration = current->explicitVariableDeclarations.find(name);
		if (declaration != current->explicitVariableDeclarations.end()) {
			const Range &range = declaration->second;
			if (current->dataVariableDeclarations.contains(name) ||
				std::pair(range.line->mergedLineIndex, range.start()) <= referencePosition)
				return current;
		}
		if (!declaredGlobalHere && current == functionScope)
			break;
	}
	return nullptr;
}

static Section *highestImplicitVariableSection(
	Section *section, const std::string &name,
	const std::unordered_map<Section *, VariableReference *> &earliestImplicitSectionReferences
) {
	Section *highest = section;
	Section *functionScope = enclosingVariableFunctionScope(section);
	bool declaredGlobalHere = functionDeclaresGlobalName(functionScope, name);
	// Lifecycle bodies can declare locals directly in the function scope.
	// Include that scope, but do not let its own references escape to a parent.
	for (Section *ancestor = section; ancestor; ancestor = ancestor->parent) {
		if (earliestImplicitSectionReferences.contains(ancestor))
			highest = ancestor;
		if (!declaredGlobalHere && ancestor == functionScope)
			break;
	}
	return highest;
}

static void materializeVariableGroup(
	const std::string &name, Section *highestSection, std::vector<VariableReference *> &groupReferences, bool isGlobal,
	VariableReference *definition
) {
	bool groupIsGlobal = isGlobal && !enclosingVariableFunctionScope(highestSection);

	auto existingDefinition = highestSection->variableDefinitions.find(name);
	if (existingDefinition != highestSection->variableDefinitions.end() && existingDefinition->second != definition) {
		VariableReference *oldDefinition = existingDefinition->second;
		auto references = highestSection->variableReferences.find(name);
		if (references != highestSection->variableReferences.end()) {
			auto &values = references->second;
			values.erase(std::remove(values.begin(), values.end(), oldDefinition), values.end());
			if (values.empty())
				highestSection->variableReferences.erase(references);
		}
	}
	highestSection->variableDefinitions[name] = definition;
	auto variable = highestSection->variables.find(name);
	if (variable == highestSection->variables.end()) {
		highestSection->variables.emplace(name, new Variable(name, definition, groupIsGlobal));
	} else {
		requireCompilerInvariant(variable->second, "section variable map contains a null variable");
		*variable->second = Variable(name, definition, groupIsGlobal);
	}
	Variable *materialized = highestSection->variables.at(name);
	for (VariableReference *reference : groupReferences) {
		if (!reference->declaredTypeConstraintName.empty())
			materialized->addDeclaredTypeConstraintReference(reference);
	}
	for (VariableReference *reference : groupReferences) {
		if (reference != definition)
			reference->definition = definition;
	}
}

// Global readers may cross their enclosing function boundary. Join every
// ancestor domain they can reach before partitioning ordinary local queries.
class BindingDomains {
	std::unordered_map<Section *, Section *> parents;

  public:
	Section *find(Section *section) {
		auto [entry, inserted] = parents.try_emplace(section, section);
		if (entry->second != section)
			entry->second = find(entry->second);
		return entry->second;
	}
	void join(Section *inner, Section *outer) { parents[find(inner)] = find(outer); }
};

static std::vector<VariableBindingGroup> evaluateBindings(const VariableBindingUnit &unit) {
	std::unordered_map<VariableReference *, Section *> explicitOwners;
	std::unordered_map<Section *, VariableReference *> earliest;
	for (VariableReference *reference : unit.references) {
		Section *section = reference->range.section();
		if (Section *owner = visibleExplicitVariableSection(section, unit.name, reference))
			explicitOwners.emplace(reference, owner);
		else
			earliest.try_emplace(section, reference); // References are in source order.
	}
	std::unordered_map<Section *, Section *> implicitOwners;
	for (const auto &[section, _] : earliest)
		implicitOwners.emplace(section, highestImplicitVariableSection(section, unit.name, earliest));
	std::map<Section *, size_t> indices;
	std::vector<VariableBindingGroup> groups;
	for (VariableReference *reference : unit.references) {
		auto explicitOwner = explicitOwners.find(reference);
		Section *owner =
			explicitOwner != explicitOwners.end() ? explicitOwner->second : implicitOwners.at(reference->range.section());
		auto [index, inserted] = indices.try_emplace(owner, groups.size());
		if (inserted)
			groups.push_back({owner, reference, {}});
		groups[index->second].references.push_back(reference);
	}
	return groups;
}
} // namespace

void materializeVariableGroups(ParseContext &context, Section *selectedDomain) {
	auto started = std::chrono::steady_clock::now();
	auto *hooks = context.variableBindingHooks;
	std::vector<std::string> names;
	for (const auto &[name, _] : context.unresolvedVariableReferences)
		names.push_back(name);
	std::sort(names.begin(), names.end());
	for (const std::string &name : names) {
		BindingDomains domains;
		auto boundary = [&](Section *section) {
			Section *function = enclosingVariableFunctionScope(section);
			return function ? function : context.mainSection;
		};
		std::vector<VariableReference *> references;
		for (VariableReference *reference : context.unresolvedVariableReferences.at(name)) {
			Section *scope = reference->range.section();
			if (context.deferredBodyResolution && context.deferredBodyResolution(scope))
				continue;
			if (selectedDomain && boundary(scope) != selectedDomain)
				continue;
			references.push_back(reference);
		}
		for (VariableReference *reference : references) {
			Section *function = enclosingVariableFunctionScope(reference->range.section());
			if (!functionDeclaresGlobalName(function, name))
				continue;
			Section *inner = function;
			for (Section *ancestor = function->parent; ancestor; ancestor = ancestor->parent)
				if (isVariableFunctionScope(ancestor) || ancestor == context.mainSection) {
					domains.join(inner, ancestor);
					inner = ancestor;
				}
		}
		std::map<Section *, size_t> indices;
		std::vector<VariableBindingUnit> units;
		for (VariableReference *reference : references) {
			Section *domain = domains.find(boundary(reference->range.section()));
			auto [index, inserted] = indices.try_emplace(domain, units.size());
			if (inserted)
				units.push_back(
					{name, domain, domain == context.mainSection && context.declaredGlobalVariables.contains(name), {}}
				);
			units[index->second].references.push_back(reference);
		}
		for (VariableBindingUnit &unit : units) {
			std::stable_sort(unit.references.begin(), unit.references.end(), [](auto *left, auto *right) {
				return variableReferenceSourcePosition(left) < variableReferenceSourcePosition(right);
			});
			auto evaluate = [&]() {
				if (hooks) {
					hooks->work.evaluated++;
					hooks->work.referencesClassified += unit.references.size();
				}
				return evaluateBindings(unit);
			};
			auto groups = hooks ? hooks->unit(unit, evaluate) : evaluate();
			for (VariableBindingGroup &group : groups)
				materializeVariableGroup(unit.name, group.owner, group.references, unit.global, group.definition);
			if (hooks) {
				hooks->work.referencesRebound += unit.references.size();
				hooks->work.groupsMaterialized += groups.size();
			}
		}
	}
	if (hooks) {
		hooks->complete(selectedDomain);
		hooks->work.milliseconds +=
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
	}
}
