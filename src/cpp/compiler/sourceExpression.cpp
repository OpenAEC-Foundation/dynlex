#include "sourceExpression.h"
#include "compilerUtils.h"
#include "expression.h"
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
