#include "semanticIdentity.h"
#include "classDefinition.h"
#include "compilerUtils.h"
#include "section.h"
#include "sourceFile.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/BLAKE3.h"
#include <set>
#include <vector>

using Json = nlohmann::json;

std::string stableIdentityDigest(std::string_view text) {
	llvm::BLAKE3 hash;
	hash.update(llvm::StringRef(text.data(), text.size()));
	return llvm::toHex(hash.final(), true);
}

Json sectionIdentity(const Section &section) {
	const CodeLine *opening = section.openingLine;
	if (!opening && !section.patternDefinitions.empty())
		opening = section.patternDefinitions.front()->range.line;
	requireCompilerInvariant(opening && opening->sourceFile, "section has no source identity");
	return Json{
		{"uri", opening->sourceFile->uri},
		{"header",
		 section.openingLine ? std::string(opening->rightTrimmedText) : section.patternDefinitions.front()->toString()},
		{"type", static_cast<int>(section.type)}
	};
}

void collectSections(Section *section, const std::string &path, std::map<std::string, Section *> &sections) {
	sections.emplace(path, section);
	std::map<std::string, size_t> duplicateIdentities;
	for (size_t index = 0; index < section->children.size(); index++) {
		Section *child = section->children[index];
		const CodeLine *source = child->openingLine;
		if (!source && !child->patternDefinitions.empty())
			source = child->patternDefinitions.front()->range.line;
		std::string component;
		if (source && source->sourceFile) {
			std::string identity = stableIdentityDigest(sectionIdentity(*child).dump());
			component = "s" + identity + "-" + std::to_string(duplicateIdentities[identity]++);
		} else {
			component = "a" + std::to_string(index);
		}
		collectSections(child, path + "/" + component, sections);
	}
}

class TypeIdentityEncoder {
	using ClassInstance = std::pair<const ClassDefinition *, int>;
	const ClassIdentityPaths &classPaths;
	TypeIdentityMode mode;
	std::set<ClassInstance> seen;
	std::vector<ClassInstance> pending;

	std::optional<Json> encodeShape(const DataType &type);

  public:
	TypeIdentityEncoder(const ClassIdentityPaths &classPaths, TypeIdentityMode mode) : classPaths(classPaths), mode(mode) {}

	std::optional<Json> encode(const DataType &type) {
		auto shape = encodeShape(type);
		if (!shape)
			return std::nullopt;
		Json layouts = Json::array();
		// Visit each reachable instantiation once. Fields add references to the
		// worklist in order, including when a class layout is recursive.
		for (size_t i = 0; i < pending.size(); i++) {
			const auto [definition, index] = pending[i];
			const auto &instantiation = definition->instantiations[index];
			if (instantiation.fieldTypes.size() != definition->fields.size())
				return std::nullopt;
			Json fields = Json::array();
			for (const DataType &field : instantiation.fieldTypes) {
				auto identity = encodeShape(field);
				if (!identity)
					return std::nullopt;
				fields.push_back(std::move(*identity));
			}
			layouts.push_back(Json::array({classPaths.at(definition), index, std::move(fields)}));
		}
		Json layoutDigest = layouts.empty() ? Json(nullptr) : Json(stableIdentityDigest(layouts.dump()));
		return Json::array({std::move(*shape), std::move(layoutDigest)});
	}
};

std::optional<Json> TypeIdentityEncoder::encodeShape(const DataType &type) {
	if (mode == TypeIdentityMode::Concrete && !type.isConcrete())
		return std::nullopt;
	Json result = Json::array({static_cast<int>(type.kind), type.numericSize, type.pointerDepth});
	switch (type.kind) {
	case DataType::Kind::Void:
	case DataType::Kind::Bool:
	case DataType::Kind::Float:
	case DataType::Kind::Int:
	case DataType::Kind::UInt:
		break;
	case DataType::Kind::Array:
	case DataType::Kind::Vector:
	case DataType::Kind::Matrix: {
		if (!type.arrayElementType)
			return std::nullopt;
		auto element = encodeShape(*type.arrayElementType);
		if (!element)
			return std::nullopt;
		result.push_back(type.arraySize);
		result.push_back(type.matrixRowCount);
		result.push_back(std::move(*element));
		break;
	}
	case DataType::Kind::Class: {
		if (!type.classDefinition ||
			(type.classInstIndex < 0 && type.pointerDepth == 0 && mode == TypeIdentityMode::Concrete) ||
			type.classInstIndex >= static_cast<int>(type.classDefinition->instantiations.size()))
			return std::nullopt;
		auto path = classPaths.find(type.classDefinition);
		if (path == classPaths.end())
			return std::nullopt;
		result.push_back(path->second);
		result.push_back(type.classInstIndex);
		ClassInstance key{type.classDefinition, type.classInstIndex};
		if (type.classInstIndex >= 0 && seen.insert(key).second)
			pending.push_back(key);
		break;
	}
	case DataType::Kind::Function: {
		if (!type.functionSignature)
			return std::nullopt;
		auto returnType = encodeShape(type.functionSignature->result);
		if (!returnType)
			return std::nullopt;
		Json arguments = Json::array();
		for (const DataType &argument : type.functionSignature->parameters) {
			auto identity = encodeShape(argument);
			if (!identity)
				return std::nullopt;
			arguments.push_back(std::move(*identity));
		}
		result.push_back(std::move(*returnType));
		result.push_back(std::move(arguments));
		break;
	}
	default:
		return std::nullopt;
	}
	return result;
}

std::optional<Json> typeIdentity(const DataType &type, const ClassIdentityPaths &classPaths, TypeIdentityMode mode) {
	return TypeIdentityEncoder(classPaths, mode).encode(type);
}
