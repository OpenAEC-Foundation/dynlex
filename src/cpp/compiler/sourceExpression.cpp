#include "sourceExpression.h"
#include "compilerUtils.h"
#include "expression.h"
#include "parseContext.h"
#include "section/variable.h"
#include "sourceOwner.h"
#include "sourceReferenceOrder.h"
#include <utility>

SourceExpression::SourceExpression(SourceExpression &&other) noexcept
	: value(std::exchange(other.value, nullptr)), product(std::exchange(other.product, nullptr)) {}

SourceExpression &SourceExpression::operator=(SourceExpression &&other) noexcept {
	value = std::exchange(other.value, nullptr);
	product = std::exchange(other.product, nullptr);
	return *this;
}

PatternReference *SourceExpression::rootReference() const {
	return product ? product->rootReference() : value ? value->patternReference : nullptr;
}

void SourceExpression::demand() const {
	if (product) {
		product->demand();
		requireCompilerInvariant(value != nullptr, "body product did not publish its requested source expression");
	}
}

void SourceExpression::missingExpression() { crashCompilerBug("source expression dereference requires an expression"); }

void SourceExpression::defer(BodySyntaxProduct &owner) {
	requireCompilerInvariant(value == nullptr && product == nullptr, "source expression was deferred after publication");
	product = &owner;
}

void SourceExpression::publish(Expression &expression) {
	requireCompilerInvariant(product != nullptr && value == nullptr, "source expression publication has no deferred product");
	value = &expression;
}

void SourceExpression::finish() {
	requireCompilerInvariant(
		product != nullptr && product->syntaxMaterialized() && value != nullptr,
		"body product finished before publishing its source expression"
	);
	product = nullptr;
}

std::vector<CodeLine *> sourceOwnerLines(const Section &owner) {
	std::vector<CodeLine *> result;
	auto collect = [&](auto &&self, const Section &scope) -> void {
		for (CodeLine *line : scope.codeLines)
			if (!line->rightTrimmedText.empty())
				result.push_back(line);
		for (Section *child : scope.children)
			if (child->patternDefinitions.empty())
				self(self, *child);
	};
	collect(collect, owner);
	std::ranges::sort(result, {}, &CodeLine::mergedLineIndex);
	return result;
}

std::vector<PatternReference *> sourceOwnerReferences(const Section &owner) {
	std::vector<PatternReference *> result;
	auto collect = [&](auto &&self, const Section &scope) -> void {
		result.insert(result.end(), scope.patternReferences.begin(), scope.patternReferences.end());
		for (Section *child : scope.children)
			if (child->patternDefinitions.empty())
				self(self, *child);
	};
	collect(collect, owner);
	orderSourceReferences(result);
	return result;
}

void ParseContext::publishSourceVariableReferences() {
	for (VariableReference *reference : pendingExplicitVariableReferences) {
		Section *section = reference->range.section();
		requireCompilerInvariant(section, "explicit variable reference has no source section");
		section->addVariableReference(*this, reference);
		if (!reference->declaredTypeConstraintName.empty() && !reference->definition)
			section->registerExplicitVariableName(reference->name, reference->range);
	}
	pendingExplicitVariableReferences.clear();
}

void ParseContext::materializeSourceOwnerReferences(Section *owner) {
	auto found = retainedSourceOwners.find(owner);
	if (found == retainedSourceOwners.end())
		return;
	auto products = std::move(found->second.products);
	retainedSourceOwners.erase(found);
	for (BodySyntaxProduct *product : products)
		product->prepareReferences();
	if (sourceReferencesReady)
		publishSourceVariableReferences();
	if (sourceOwnerReferencesPublished)
		sourceOwnerReferencesPublished(owner, sourceOwnerReferences(*owner));
}
