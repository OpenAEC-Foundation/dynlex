#include "numericLiteral.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <charconv>
#include <limits>
#include <system_error>

namespace {

bool isDecimalFloatingLiteral(std::string_view text) {
	size_t position = text.starts_with('-') ? 1 : 0;
	auto consumeDigits = [&]() {
		size_t start = position;
		while (position < text.size() && text[position] >= '0' && text[position] <= '9')
			position++;
		return position != start;
	};
	bool hasDigits = consumeDigits();
	if (position < text.size() && text[position] == '.') {
		position++;
		hasDigits = consumeDigits() || hasDigits;
	}
	if (!hasDigits)
		return false;
	if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
		position++;
		if (position < text.size() && (text[position] == '+' || text[position] == '-'))
			position++;
		if (!consumeDigits())
			return false;
	}
	return position == text.size();
}

} // namespace

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

	// APFloat also accepts hexadecimal, special values and empty exponents.
	if (!isDecimalFloatingLiteral(text))
		return {{0.0}, NumericLiteralParseError::Invalid};
	llvm::APFloat value(0.0);
	auto status = value.convertFromString(llvm::StringRef(text.data(), text.size()), llvm::APFloat::rmNearestTiesToEven);
	if (!status) {
		llvm::consumeError(status.takeError());
		return {{0.0}, NumericLiteralParseError::Invalid};
	}
	// Inexact subnormals are representable; only underflow rounded to zero is out of range.
	if (!value.isFinite() || ((*status & llvm::APFloat::opUnderflow) && value.isZero()))
		return {{0.0}, NumericLiteralParseError::FloatingPointOutOfRange};
	return {{value.convertToDouble()}, NumericLiteralParseError::None};
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
