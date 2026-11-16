#include "sourceLineIdentity.h"
#include "codeLine.h"
#include "parseContext.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/BLAKE3.h"

std::vector<SourceLineIdentity>
sourceLineIdentities(const ParseContext &context, const std::map<const Section *, std::string> &paths) {
	std::vector<SourceLineIdentity> result;
	result.reserve(context.codeLines.size());
	std::map<std::pair<std::string, std::string>, size_t> occurrences;
	for (CodeLine *line : context.codeLines) {
		if (!line->section)
			continue; // Empty preprocessing lines have no semantic owner.
		const std::string &owner = paths.at(line->section);
		llvm::BLAKE3 hash;
		hash.update(llvm::StringRef(line->rightTrimmedText));
		std::string text = llvm::toHex(hash.final(), true);
		result.push_back({line, nlohmann::json::array({owner, text, occurrences[{owner, text}]++})});
	}
	return result;
}
