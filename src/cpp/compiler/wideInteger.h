#pragma once
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallString.h"
#include <compare>
#include <string>

// Fixed-width integer bits, with their interpretation retained across constant
// evaluation, specialization keys and persisted inference results.
struct WideInteger {
	llvm::APInt bits;
	bool isUnsigned = false;

	std::string toString(unsigned radix = 10) const {
		llvm::SmallString<64> text;
		bits.toString(text, radix, !isUnsigned);
		return std::string(text);
	}

	bool operator==(const WideInteger &other) const {
		return isUnsigned == other.isUnsigned && bits.getBitWidth() == other.bits.getBitWidth() && bits == other.bits;
	}
	std::strong_ordering operator<=>(const WideInteger &other) const {
		if (auto order = bits.getBitWidth() <=> other.bits.getBitWidth(); order != 0)
			return order;
		if (auto order = isUnsigned <=> other.isUnsigned; order != 0)
			return order;
		return bits == other.bits	  ? std::strong_ordering::equal
			   : bits.ult(other.bits) ? std::strong_ordering::less
									  : std::strong_ordering::greater;
	}
};
