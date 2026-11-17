#include "sourceIdentity.h"
#include "pathUtils.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/BLAKE3.h"
#include <filesystem>

std::string digest(std::string_view text) {
	llvm::BLAKE3 hash;
	hash.update(llvm::StringRef(text.data(), text.size()));
	return llvm::toHex(hash.final(), true);
}

std::string canonicalSourceKey(std::string_view path) {
	std::filesystem::path fsPath = pathutil::toFilesystemPath(path);
	std::error_code ec;
	std::filesystem::path canonical = std::filesystem::weakly_canonical(fsPath, ec);
	if (ec) {
		canonical = std::filesystem::absolute(fsPath, ec);
		if (ec)
			return fsPath.lexically_normal().string();
	}
	return canonical.lexically_normal().string();
}
