#pragma once

static std::int64_t compileTimeBitwiseNot(std::int64_t value) {
	return static_cast<std::int64_t>(~static_cast<std::uint64_t>(value));
}

static std::int64_t compileTimeShiftRight(std::int64_t value, unsigned amount) {
	if (amount == 0)
		return value;
	std::uint64_t bits = static_cast<std::uint64_t>(value);
	bits >>= amount;
	if (value < 0)
		bits |= (~std::uint64_t{0}) << (64 - amount);
	return static_cast<std::int64_t>(bits);
}

static std::int64_t normalizeSignedIntegerToType(std::int64_t value, const DataType &type) {
	requireCompilerInvariant(type.isInteger() && type.numericSize > 0, "integer type has no concrete width");
	unsigned bitCount = static_cast<unsigned>(type.numericSize * 8);
	requireCompilerInvariant(bitCount <= 64, "compile-time integer is wider than 64 bits");
	if (bitCount == 64)
		return value;
	std::uint64_t mask = (std::uint64_t{1} << bitCount) - 1;
	std::uint64_t truncated = static_cast<std::uint64_t>(value) & mask;
	std::uint64_t signBit = std::uint64_t{1} << (bitCount - 1);
	if (!(truncated & signBit))
		return static_cast<std::int64_t>(truncated);
	std::uint64_t magnitude = ((~truncated) & mask) + 1;
	return -static_cast<std::int64_t>(magnitude);
}

static std::uint64_t integerMask(const DataType &type) {
	requireCompilerInvariant(type.isInteger() && type.numericSize > 0, "integer type has no concrete width");
	unsigned bitCount = static_cast<unsigned>(type.numericSize * 8);
	return bitCount == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bitCount) - 1;
}

static std::uint64_t normalizeIntegerBitsToType(std::uint64_t bits, const DataType &type) { return bits & integerMask(type); }

static std::int64_t signedIntegerFromBits(std::uint64_t bits, const DataType &type) {
	bits = normalizeIntegerBitsToType(bits, type);
	if (type.numericSize == 8)
		return static_cast<std::int64_t>(bits);
	std::uint64_t signBit = std::uint64_t{1} << (type.numericSize * 8 - 1);
	return (bits & signBit) ? static_cast<std::int64_t>(bits | ~integerMask(type)) : static_cast<std::int64_t>(bits);
}

static CompileTimeValue compileTimeIntegerFromBits(std::uint64_t bits, const DataType &type) {
	bits = normalizeIntegerBitsToType(bits, type);
	return type.isUnsignedInteger() ? CompileTimeValue(bits) : CompileTimeValue(signedIntegerFromBits(bits, type));
}
