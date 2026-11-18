#include "sourceLineIdentity.h"
#include "codeLine.h"
#include "parseContext.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/BLAKE3.h"

SourceLineIdentity SourceLineIdentityBuilder::append(CodeLine *line) {
	llvm::BLAKE3 hash;
	hash.update(llvm::StringRef(line->rightTrimmedText));
	std::string text = llvm::toHex(hash.final(), true);
	return {line, nlohmann::json::array({owner, text, occurrences[text]++})};
}

std::vector<SourceLineIdentity>
sourceLineIdentities(const ParseContext &context, const std::map<const Section *, std::string> &paths) {
	std::vector<SourceLineIdentity> result;
	result.reserve(context.codeLines.size());
	std::map<const Section *, SourceLineIdentityBuilder> owners;
	for (CodeLine *line : context.codeLines) {
		if (!line->section)
			continue; // Empty preprocessing lines have no semantic owner.
		auto [owner, inserted] = owners.try_emplace(line->section, paths.at(line->section));
		(void)inserted;
		result.push_back(owner->second.append(line));
	}
	return result;
}
