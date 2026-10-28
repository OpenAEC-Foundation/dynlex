#include "numericLiteral.h"
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <string>
#include <system_error>

NumericLiteralParseResult parseNumericLiteral(std::string_view text) {
	if (text.empty())
		return {{std::int64_t{0}}, NumericLiteralParseError::Invalid};

	if (text.find('.') == std::string_view::npos && text.find('e') == std::string_view::npos &&
		text.find('E') == std::string_view::npos) {
		std::uint64_t magnitude = 0;
		auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), magnitude);
		if (error == std::errc::result_out_of_range)
			return {{std::int64_t{0}}, NumericLiteralParseError::UnsignedIntegerOutOfRange};
		if (error != std::errc{} || end != text.data() + text.size())
			return {{std::int64_t{0}}, NumericLiteralParseError::Invalid};
		constexpr std::uint64_t maximumSignedInteger = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
		if (magnitude <= maximumSignedInteger)
			return {{static_cast<std::int64_t>(magnitude)}, NumericLiteralParseError::None};
		if (magnitude == maximumSignedInteger + 1)
			return {
				{MinimumSignedIntegerMagnitude{std::make_shared<const MinimumSignedIntegerMagnitudeIdentity>()}},
				NumericLiteralParseError::None
			};
		return {{magnitude}, NumericLiteralParseError::None};
	}

	// `strtod` reports ERANGE both for overflow and for any underflow, including
	// representable subnormal values; only overflow and underflow of a nonzero
	// literal to zero are out of range.
	std::string literal(text);
	errno = 0;
	char *end = nullptr;
	double value = std::strtod(literal.c_str(), &end);
	if (end != literal.c_str() + literal.size())
		return {{0.0}, NumericLiteralParseError::Invalid};
	if (!std::isfinite(value))
		return {{0.0}, NumericLiteralParseError::FloatingPointOutOfRange};
	if (errno == ERANGE && value == 0.0 && literal.find_first_of("123456789") < literal.find_first_of("eE"))
		return {{0.0}, NumericLiteralParseError::FloatingPointOutOfRange};
	return {{value}, NumericLiteralParseError::None};
}

DataType numericLiteralType(const NumericLiteralValue &value, bool emitSPIRV) {
	if (const auto *integer = std::get_if<std::int64_t>(&value)) {
		int byteSize =
			*integer >= std::numeric_limits<std::int32_t>::min() && *integer <= std::numeric_limits<std::int32_t>::max() ? 4
																														 : 8;
		return {DataType::Kind::Int, byteSize};
	}
	if (std::holds_alternative<std::uint64_t>(value))
		return {DataType::Kind::UInt, 8};
	if (std::holds_alternative<MinimumSignedIntegerMagnitude>(value))
		return {DataType::Kind::UInt, 8};
	return defaultFloatType(emitSPIRV);
}

CompileTimeValue numericLiteralCompileTimeValue(const NumericLiteralValue &value) {
	if (const auto *integer = std::get_if<std::int64_t>(&value))
		return *integer;
	if (const auto *integer = std::get_if<std::uint64_t>(&value))
		return *integer;
	if (const auto *minimumMagnitude = std::get_if<MinimumSignedIntegerMagnitude>(&value))
		return *minimumMagnitude;
	return std::get<double>(value);
}
