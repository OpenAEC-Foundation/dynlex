#include "sourceExpressionSite.h"
#include "expression.h"

Expression *SourceExpressionSite::get() const { return source ? source->expression(node) : value; }
Expression *SourceExpressionSite::materialized() const { return source ? source->materializedExpression(node) : value; }
Range SourceExpressionSite::range() const { return source ? source->range(node) : value->range; }
size_t SourceExpressionSite::argumentCount() const { return source ? source->argumentCount(node) : value->arguments.size(); }
SourceExpressionSite SourceExpressionSite::argument(size_t index) const {
	return source ? SourceExpressionSite(*source, source->argument(node, index))
				  : SourceExpressionSite(value->arguments.at(index));
}
std::optional<std::string> SourceExpressionSite::numericSpelling() const {
	if (source)
		return source->numericSpelling(node);
	return value ? numericSourceExpressionSpelling(*value) : std::nullopt;
}
std::optional<std::string> numericSourceExpressionSpelling(const Expression &value) {
	if (value.kind != Expression::Kind::Literal || !value.range.line)
		return {};
	const auto &literal = value.literalValue;
	bool numeric = std::holds_alternative<std::int64_t>(literal) || std::holds_alternative<std::uint64_t>(literal) ||
				   std::holds_alternative<WideInteger>(literal) ||
				   std::holds_alternative<MinimumSignedIntegerMagnitude>(literal) || std::holds_alternative<double>(literal);
	return numeric ? std::optional<std::string>(value.range.subString) : std::nullopt;
}
bool SourceExpressionSite::operator==(const SourceExpressionSite &other) const {
	if (source || other.source)
		return source == other.source && node == other.node;
	return value == other.value;
}
