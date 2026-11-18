#pragma once
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/BLAKE3.h"
#include <string>
#include <string_view>

inline std::string stableIdentityDigest(std::string_view text) {
	llvm::BLAKE3 hash;
	hash.update(llvm::StringRef(text));
	return llvm::toHex(hash.final(), true);
}
