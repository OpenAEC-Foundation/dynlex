#include "compiler/numericLiteral.h"
#include <bit>
#include <cfenv>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

void expect(bool condition, std::string_view input) {
	if (condition)
		return;
	std::cerr << "Unexpected numeric literal result: " << input << '\n';
	std::exit(1);
}

void expectFloat(std::string_view input, double expected) {
	NumericLiteralParseResult parsed = parseNumericLiteral(input);
	expect(parsed && std::holds_alternative<double>(parsed.value), input);
	expect(std::bit_cast<std::uint64_t>(std::get<double>(parsed.value)) == std::bit_cast<std::uint64_t>(expected), input);
}

} // namespace

int main() {
	// Decimal conversion must not depend on the host's active rounding mode.
	int originalRounding = std::fegetround();
	expect(originalRounding != -1, "read rounding mode");
	for (int rounding : {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO}) {
		expect(std::fesetround(rounding) == 0, "set rounding mode");
		expectFloat("0.1", 0.1);
		expectFloat("1.00000000000000011102230246251565404236316680908203125", 1.0);
		expectFloat("5e-324", std::numeric_limits<double>::denorm_min());
		expectFloat("4.9406564584124654e-324", std::numeric_limits<double>::denorm_min());
		expectFloat("2.225073858507201e-308", 0x0.fffffffffffffp-1022);
		expectFloat("2.2250738585072014e-308", std::numeric_limits<double>::min());
		expectFloat("1.7976931348623157e308", std::numeric_limits<double>::max());
	}
	expect(std::fesetround(originalRounding) == 0, "restore rounding mode");
	expectFloat("0.0", 0.0);
	expectFloat("-0.0", -0.0);
	expectFloat("0e999999", 0.0);
	expectFloat("0e-999999", 0.0);
	expectFloat("1E+2", 100.0);
	expectFloat(".5", 0.5);
	expectFloat("1.", 1.0);
	for (std::string_view input : {"1e309", "1e999999", "1e-324", "1e-999999", "-1e-324"})
		expect(parseNumericLiteral(input).error == NumericLiteralParseError::FloatingPointOutOfRange, input);
	for (std::string_view input : {"", "1.0x", "1e", "1e+", "1e--2", "1.2.3", " 1.0", "1.0 ", "+1.0", "0x1.0p2"})
		expect(parseNumericLiteral(input).error == NumericLiteralParseError::Invalid, input);
	expect(std::get<std::int64_t>(parseNumericLiteral("9223372036854775807").value) == INT64_MAX, "maximum signed integer");
	expect(
		std::holds_alternative<MinimumSignedIntegerMagnitude>(parseNumericLiteral("9223372036854775808").value),
		"minimum signed integer magnitude"
	);
	expect(
		std::get<std::uint64_t>(parseNumericLiteral("18446744073709551615").value) == UINT64_MAX, "maximum unsigned integer"
	);
	expect(
		parseNumericLiteral("18446744073709551616").error == NumericLiteralParseError::UnsignedIntegerOutOfRange,
		"integer overflow"
	);
	return 0;
}
