#pragma once
#include "compileTimeInfo.h"
#include "type.h"
#include <cstdint>
#include <string_view>
#include <variant>

using NumericLiteralValue = std::variant<std::int64_t, MinimumSignedIntegerMagnitude, double>;

enum class NumericLiteralParseError {
	None,
	IntegerOutOfRange,
	FloatingPointOutOfRange,
	Invalid,
};

struct NumericLiteralParseResult {
	NumericLiteralValue value{std::int64_t{0}};
	NumericLiteralParseError error = NumericLiteralParseError::None;

	explicit operator bool() const { return error == NumericLiteralParseError::None; }
};

NumericLiteralParseResult parseNumericLiteral(std::string_view text);
DataType numericLiteralType(const NumericLiteralValue &value, bool emitSPIRV);
CompileTimeValue numericLiteralCompileTimeValue(const NumericLiteralValue &value);
void recordConsumedMinimumSignedIntegerMagnitude(MinimumSignedIntegerMagnitudeEffects &effects, const CompileTimeValue &value);
void recordRejectedMinimumSignedIntegerMagnitudeUse(
	MinimumSignedIntegerMagnitudeEffects &effects, const CompileTimeValue &value
);
