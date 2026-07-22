#pragma once

#include "bindingResolution.h"
#include "section.h"
#include "variable.h"
#include <algorithm>
#include <vector>

inline bool sectionIsDescendantOrSame(Section *section, Section *ancestor) {
	return section && ancestor && (section == ancestor || section->isDescendantOf(ancestor));
}

template <typename Frame>
Frame *resolveSectionFlexBodyFrame(
	std::vector<Frame> &frames, Section *callSection, const std::vector<Section *> &flexCallSiteSections,
	const std::vector<Section *> &activeFlexDefinitions, Section **executionSection = nullptr
) {
	auto findOwner = [&](Section *ownerSection) -> Frame * {
		if (!ownerSection)
			return nullptr;
		for (auto frame = frames.rbegin(); frame != frames.rend(); ++frame) {
			if (sectionIsDescendantOrSame(ownerSection, frame->definitionSection)) {
				if (executionSection)
					*executionSection = ownerSection;
				return &*frame;
			}
		}
		return nullptr;
	};

	if (Frame *frame = findOwner(callSection))
		return frame;
	for (auto owner = flexCallSiteSections.rbegin(); owner != flexCallSiteSections.rend(); ++owner) {
		if (Frame *frame = findOwner(*owner))
			return frame;
	}
	for (auto definition = activeFlexDefinitions.rbegin(); definition != activeFlexDefinitions.rend(); ++definition) {
		if (!*definition || (*definition)->type != SectionType::Section)
			continue;
		for (auto frame = frames.rbegin(); frame != frames.rend(); ++frame) {
			if (frame->definitionSection == *definition) {
				if (executionSection)
					*executionSection = *definition;
				return &*frame;
			}
		}
	}
	return nullptr;
}

inline Expression *
sectionFlexVariableExpression(InstantiatedSectionBody *definitionBody, VariableReference *definitionReference) {
	if (!definitionBody || !definitionReference)
		return nullptr;
	VariableReference *normalizedDefinition = normalizeBindingReference(definitionReference);
	Expression *result = nullptr;
	for (Expression *root : definitionBody->lineExpressions) {
		visitExpressionTree(root, [&](Expression *expression) {
			if (expression->kind != Expression::Kind::Variable || !expression->variable ||
				normalizeBindingReference(expression->variable) != normalizedDefinition) {
				return false;
			}
			result = expression;
			return true;
		});
		if (result)
			return result;
	}
	for (const auto &childBody : definitionBody->childBodies) {
		if (Expression *expression = sectionFlexVariableExpression(childBody.get(), normalizedDefinition))
			return expression;
	}
	return nullptr;
}

inline BindingFrame sectionFlexCallerVariableBindings(
	Section *definitionSection, InstantiatedSectionBody *definitionBody, Section *executionSection, Section *callerBodySection
) {
	BindingFrame bindings;
	if (!definitionSection || !definitionBody || !executionSection || !callerBodySection ||
		!sectionIsDescendantOrSame(executionSection, definitionSection)) {
		return bindings;
	}
	std::vector<Section *> activeScopes;
	for (Section *scope = executionSection; scope && scope != definitionSection; scope = scope->parent)
		activeScopes.push_back(scope);
	std::ranges::reverse(activeScopes);
	for (Section *scope : activeScopes) {
		for (const auto &[name, definitionReference] : scope->variableDefinitions) {
			Variable *callerVariable = callerBodySection->findVariable(name);
			Expression *definitionExpression = sectionFlexVariableExpression(definitionBody, definitionReference);
			if (!callerVariable || !callerVariable->definition || !definitionExpression)
				continue;
			bindings.bindings[name] = definitionExpression;
			bindings.parameterBindings[normalizeBindingReference(callerVariable->definition)] = definitionExpression;
		}
	}
	return bindings;
}

inline void pushSectionFlexCallerVariableBindings(
	BindingFrameStack &bindingFrameStack, Section *definitionSection, InstantiatedSectionBody *definitionBody,
	Section *executionSection, Section *callerBodySection
) {
	BindingFrame bindings =
		sectionFlexCallerVariableBindings(definitionSection, definitionBody, executionSection, callerBodySection);
	if (!bindings.empty())
		pushBindingScope(bindingFrameStack, std::move(bindings));
}
