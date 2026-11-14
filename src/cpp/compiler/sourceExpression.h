#pragma once

struct Expression;
struct PatternReference;
struct Section;

// A body product owns its input proof and publishes its source expressions
// together. A consumer cannot observe a partially materialized body.
struct BodySyntaxProduct {
	virtual ~BodySyntaxProduct() = default;
	virtual void demand() = 0;
	virtual bool syntaxMaterialized() const = 0;
	virtual PatternReference *rootReference() const = 0;
	virtual const Section &bodyOwner() const = 0;
};

class SourceExpression {
  public:
	SourceExpression() = default;
	SourceExpression(const SourceExpression &) = delete;
	SourceExpression &operator=(const SourceExpression &) = delete;
	SourceExpression(SourceExpression &&other) noexcept;
	SourceExpression &operator=(SourceExpression &&other) noexcept;
	SourceExpression &operator=(Expression *expression) {
		if (product)
			demand();
		value = expression;
		return *this;
	}
	operator Expression *&() {
		if (product)
			demand();
		return value;
	}
	operator Expression * const &() const {
		if (product)
			demand();
		return value;
	}
	explicit operator bool() { return value || product; }
	explicit operator bool() const { return value || product; }
	Expression *operator->() const {
		if (product)
			demand();
		if (!value)
			missingExpression();
		return value;
	}

	Expression *materialized() const { return value; }
	PatternReference *rootReference() const;
	bool deferred() const { return product != nullptr; }
	void defer(BodySyntaxProduct &owner);
	void publish(Expression &expression);
	void finish();

  private:
	void demand() const;
	[[noreturn]] static void missingExpression();
	Expression *value = nullptr;
	BodySyntaxProduct *product = nullptr;
};
