#pragma once

static void refineDirectMinimumSignedLiteralType(Expression *expression, const CompileTimeValue &value) {
	if (!expression || !std::holds_alternative<std::int64_t>(value) ||
		std::get<std::int64_t>(value) != std::numeric_limits<std::int64_t>::min())
		return;
	bool directMinimumLiteral = std::ranges::any_of(expression->arguments, [](Expression *argument) {
		return argument && argument->kind == Expression::Kind::Literal &&
			   std::holds_alternative<MinimumSignedIntegerMagnitude>(argument->literalValue);
	});
	if (directMinimumLiteral)
		expression->type = {DataType::Kind::Int, 8};
}
