#pragma once

#include "bindingResolution.h"
#include "section.h"
#include "variable.h"
#include "variableStorageKey.h"

inline VariableStorageKey variableStorageKey(VariableReference *reference, const InstantiatedSectionBody *body) {
	VariableReference *definition = normalizeBindingReference(reference);
	requireCompilerInvariant(definition != nullptr, "variable storage requires a declaration");
	Variable *variable = definition->range.line->section->findVariable(definition);
	if (variable && variable->isGlobal)
		return {definition, nullptr};
	for (const InstantiatedSectionBody *scope = body; scope; scope = scope->parentBody) {
		auto declared = scope->sourceSection->variableDefinitions.find(definition->name);
		if (declared != scope->sourceSection->variableDefinitions.end() &&
			normalizeBindingReference(declared->second) == definition)
			return {definition, scope->storageScope};
	}
	return {definition, nullptr};
}

inline VariableStorageKey variableStorageKey(const Expression *expression) {
	requireCompilerInvariant(expression != nullptr, "variable storage requires an expression");
	return variableStorageKey(expression->variable, expression->owningSectionBody);
}
