#pragma once

#include "lsp/sourceFile.h"
#include "section.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/BLAKE3.h"
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

inline nlohmann::json sectionIdentity(const Section &section) {
	const CodeLine *opening = section.openingLine;
	if (!opening && !section.patternDefinitions.empty())
		opening = section.patternDefinitions.front()->range.line;
	requireCompilerInvariant(opening && opening->sourceFile, "section has no source identity");
	return nlohmann::json{
		{"uri", opening->sourceFile->uri},
		{"header",
		 section.openingLine ? std::string(opening->rightTrimmedText) : section.patternDefinitions.front()->toString()},
		{"type", static_cast<int>(section.type)}
	};
}

inline std::string sectionIdentityDigest(std::string_view text) {
	llvm::BLAKE3 hash;
	hash.update(llvm::StringRef(text));
	return llvm::toHex(hash.final(), true);
}

template <typename Visitor> void visitSectionIdentities(Section *section, const std::string &path, const Visitor &visit) {
	visit(section, path);
	std::map<std::string, size_t> duplicateIdentities;
	for (size_t index = 0; index < section->children.size(); index++) {
		Section *child = section->children[index];
		const CodeLine *source = child->openingLine;
		if (!source && !child->patternDefinitions.empty())
			source = child->patternDefinitions.front()->range.line;
		std::string component;
		if (source && source->sourceFile) {
			std::string identity = sectionIdentityDigest(sectionIdentity(*child).dump());
			component = "s" + identity + "-" + std::to_string(duplicateIdentities[identity]++);
		} else {
			component = "a" + std::to_string(index);
		}
		visitSectionIdentities(child, path + "/" + component, visit);
	}
}

inline bool hasCodegenSectionIdentity(const Section &section) {
	return (section.type == SectionType::Function && !section.isFlex && !section.patternDefinitions.empty()) ||
		   section.type == SectionType::Class || section.type == SectionType::Retain || section.type == SectionType::Release;
}
