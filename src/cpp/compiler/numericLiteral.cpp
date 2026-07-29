#include "numericLiteral.h"
#include "compilerUtils.h"
#include <charconv>
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
			return {{std::int64_t{0}}, NumericLiteralParseError::IntegerOutOfRange};
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
		return {{std::int64_t{0}}, NumericLiteralParseError::IntegerOutOfRange};
	}

	try {
		size_t parsedLength = 0;
		double value = std::stod(std::string(text), &parsedLength);
		if (parsedLength != text.size())
			return {{0.0}, NumericLiteralParseError::Invalid};
		if (!std::isfinite(value))
			return {{0.0}, NumericLiteralParseError::FloatingPointOutOfRange};
		return {{value}, NumericLiteralParseError::None};
	} catch (const std::out_of_range &) {
		return {{0.0}, NumericLiteralParseError::FloatingPointOutOfRange};
	} catch (const std::invalid_argument &) {
		return {{0.0}, NumericLiteralParseError::Invalid};
	}
}

DataType numericLiteralType(const NumericLiteralValue &value, bool emitSPIRV) {
	if (const auto *integer = std::get_if<std::int64_t>(&value)) {
		int byteSize =
			*integer >= std::numeric_limits<std::int32_t>::min() && *integer <= std::numeric_limits<std::int32_t>::max() ? 4
																														 : 8;
		return {DataType::Kind::Int, byteSize};
	}
	if (std::holds_alternative<MinimumSignedIntegerMagnitude>(value))
		return {DataType::Kind::Int, 8};
	return defaultFloatType(emitSPIRV);
}

CompileTimeValue numericLiteralCompileTimeValue(const NumericLiteralValue &value) {
	if (const auto *integer = std::get_if<std::int64_t>(&value))
		return *integer;
	if (const auto *minimumMagnitude = std::get_if<MinimumSignedIntegerMagnitude>(&value))
		return *minimumMagnitude;
	return std::get<double>(value);
}

void recordConsumedMinimumSignedIntegerMagnitude(MinimumSignedIntegerMagnitudeEffects &effects, const CompileTimeValue &value) {
	if (const auto *minimumMagnitude = std::get_if<MinimumSignedIntegerMagnitude>(&value)) {
		requireCompilerInvariant(minimumMagnitude->identity != nullptr, "minimum integer magnitude has no identity");
		if (!containsMinimumSignedIntegerMagnitudeIdentity(effects.consumedByNegation, minimumMagnitude->identity))
			effects.consumedByNegation.push_back(minimumMagnitude->identity);
	}
}

void recordRejectedMinimumSignedIntegerMagnitudeUse(
	MinimumSignedIntegerMagnitudeEffects &effects, const CompileTimeValue &value
) {
	if (const auto *minimumMagnitude = std::get_if<MinimumSignedIntegerMagnitude>(&value)) {
		requireCompilerInvariant(minimumMagnitude->identity != nullptr, "minimum integer magnitude has no identity");
		if (!containsMinimumSignedIntegerMagnitudeIdentity(effects.rejectedUses, minimumMagnitude->identity))
			effects.rejectedUses.push_back(minimumMagnitude->identity);
	}
}
