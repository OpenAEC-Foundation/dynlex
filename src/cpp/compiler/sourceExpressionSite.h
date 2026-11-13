#pragma once
#include "range.h"
#include <cstddef>
#include <optional>
#include <string>

struct Expression;
struct SourceExpressionSite;
std::optional<std::string> numericSourceExpressionSpelling(const Expression &expression);

// Matching needs source identity, grouping and numeric spelling, independently
// of whether a consumer has requested the expression tree.
struct DeferredExpressionSource {
	virtual ~DeferredExpressionSource() = default;
	virtual Expression *expression(size_t node) = 0;
	virtual Expression *materializedExpression(size_t node) const = 0;
	virtual Range range(size_t node) const = 0;
	virtual size_t argumentCount(size_t node) const = 0;
	virtual size_t argument(size_t node, size_t index) const = 0;
	virtual std::optional<std::string> numericSpelling(size_t node) const = 0;
};

struct SourceExpressionSite {
	SourceExpressionSite() = default;
	SourceExpressionSite(std::nullptr_t) {}
	SourceExpressionSite(Expression *value) : value(value) {}
	SourceExpressionSite(DeferredExpressionSource &source, size_t node) : source(&source), node(node) {}
	Expression *get() const;
	Expression *materialized() const;
	operator Expression *() const { return get(); }
	Expression *operator->() const { return get(); }
	explicit operator bool() const { return value || source; }
	Range range() const;
	size_t argumentCount() const;
	SourceExpressionSite argument(size_t index) const;
	std::optional<std::string> numericSpelling() const;
	bool isRoot() const { return source && node == 0; }
	bool deferred() const { return source != nullptr; }
	bool operator==(const SourceExpressionSite &other) const;
	bool operator==(std::nullptr_t) const { return !static_cast<bool>(*this); }

  private:
	Expression *value = nullptr;
	DeferredExpressionSource *source = nullptr;
	size_t node = 0;
};
