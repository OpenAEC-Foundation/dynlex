#include "incrementalMatcherCache.h"
#include "compilerUtils.h"
#include "dataSections.h"
#include "lsp/sourceFile.h"
#include "parseContext.h"
#include "patternDefinition.h"
#include "patternReference.h"
#include "section.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/SHA256.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace {
using Json = IncrementalMatcherCache::Json;
constexpr std::string_view matcherCacheHeader = "DynLex matcher results v1\n";

void appendLength(std::string &output, std::uint64_t value) {
	for (unsigned index = 0; index < 8; index++) {
		output.push_back(static_cast<char>(value & 0xff));
		value >>= 8;
	}
}

std::uint64_t readLength(std::string_view input, size_t &position) {
	if (input.size() - position < 8)
		throw std::runtime_error("truncated incremental matcher cache length");
	std::uint64_t value = 0;
	for (unsigned index = 0; index < 8; index++)
		value |= static_cast<std::uint64_t>(static_cast<unsigned char>(input[position++])) << (index * 8);
	return value;
}

std::string digest(std::string_view value) {
	llvm::SHA256 hash;
	hash.update(llvm::StringRef(value.data(), value.size()));
	return llvm::toHex(hash.final(), true);
}

class Fingerprint {
  public:
	Fingerprint() { buffer.reserve(2048); }
	void number(std::uint64_t value) {
		std::array<char, 8> bytes;
		for (size_t index = 0; index < bytes.size(); index++) {
			bytes[index] = static_cast<char>(value & 0xff);
			value >>= 8;
		}
		buffer.append(bytes.data(), bytes.size());
	}
	void boolean(bool value) { number(value); }
	void string(std::string_view value) {
		number(value.size());
		if (!value.empty())
			buffer.append(value.data(), value.size());
	}
	std::string finish() const { return digest(buffer); }

  private:
	std::string buffer;
};

void appendDefinition(Fingerprint &key, const PatternDefinition *definition) {
	requireCompilerInvariant(
		definition && definition->range.line && definition->range.line->sourceFile,
		"incremental matcher encountered a definition without a source location"
	);
	const CodeLine *line = definition->range.line;
	const Section *section = definition->section;
	key.string(line->sourceFile->uri);
	key.number(line->sourceFileLineIndex);
	key.number(definition->range.start());
	key.number(definition->range.end());
	key.string(line->fullText);
	key.string(definition->toString());
	key.number(static_cast<int>(section->type));
	key.boolean(section->isLocal);
	key.boolean(section->isConversion);
	key.string(section->dataSection ? std::string_view(section->dataSection->declarationParameter) : std::string_view{});
	key.boolean(section->dataFieldOwner != nullptr);
	if (section->dataFieldOwner) {
		const CodeLine *opening = section->dataFieldOwner->openingLine;
		requireCompilerInvariant(opening && opening->sourceFile, "data field matcher definition has no owning source section");
		key.string(opening->sourceFile->uri);
		key.number(opening->sourceFileLineIndex);
		key.string(opening->fullText);
		key.number(static_cast<int>(section->dataFieldOwner->type));
	}
}

void appendSourcePosition(Fingerprint &key, const Range &range) {
	requireCompilerInvariant(range.line && range.line->sourceFile, "incremental matcher source position has no source file");
	key.string(range.line->sourceFile->uri);
	key.number(range.line->sourceFileLineIndex);
	key.number(range.line->mergedLineIndex);
	key.number(range.start());
	key.number(range.end());
}
} // namespace

IncrementalMatcherCache::IncrementalMatcherCache(ParseContext &context, std::string_view previous) : context(context) {
	if (previous.empty())
		return;
	if (!previous.starts_with(matcherCacheHeader))
		throw std::runtime_error("invalid incremental matcher cache header");
	size_t position = matcherCacheHeader.size();
	while (position < previous.size()) {
		if (previous.size() - position < 64)
			throw std::runtime_error("truncated incremental matcher cache key");
		std::string_view cacheKey = previous.substr(position, 64);
		position += 64;
		if (!std::all_of(cacheKey.begin(), cacheKey.end(), [](char character) {
			return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
		}))
			throw std::runtime_error("invalid incremental matcher cache key");
		std::uint64_t length = readLength(previous, position);
		if (length > previous.size() - position)
			throw std::runtime_error("truncated incremental matcher cache value");
		auto [_, inserted] = previousEntries.emplace(cacheKey, previous.substr(position, static_cast<size_t>(length)));
		if (!inserted)
			throw std::runtime_error("duplicate incremental matcher cache key");
		position += static_cast<size_t>(length);
	}
}

std::string IncrementalMatcherCache::snapshot() const {
	std::string result(matcherCacheHeader);
	result.reserve(recorded.size() * 256);
	for (const auto &[key, value] : recorded) {
		result += key;
		std::string_view entry = std::holds_alternative<std::string_view>(value)
									 ? std::get<std::string_view>(value)
									 : std::string_view(std::get<std::string>(value));
		appendLength(result, entry.size());
		result.append(entry.data(), entry.size());
	}
	return result;
}

std::string IncrementalMatcherCache::sourceDigest(const lsp::SourceFile *source) {
	requireCompilerInvariant(source != nullptr, "incremental matcher source is missing");
	auto [it, inserted] = sourceDigests.try_emplace(source);
	if (inserted)
		it->second = digest(source->content);
	return it->second;
}

bool IncrementalMatcherCache::available(MatchOptions options) {
	if (options.acceptLiterals || options.maxSteps ||
		context.compilationStage >= ParseContext::CompilationStage::ResolvedPatternPrecedence)
		return false;
	std::vector<size_t> revisions;
	for (const PatternTreeNode *root : context.patternTrees) {
		if (!root)
			return false;
		revisions.push_back(root->mutationRevision);
	}
	if (revisions != treeRevisions) {
		std::string state;
		for (const PatternTreeNode *root : context.patternTrees)
			state += root->mutationDigest + ":";
		treeDigest = digest(state);
		treeRevisions = std::move(revisions);
	}
	return true;
}

std::string IncrementalMatcherCache::key(const PatternReference &reference, bool trackDependencies) {
	const Range &range = reference.range();
	requireCompilerInvariant(range.line && range.line->sourceFile, "incremental matcher reference has no source");
	Fingerprint fingerprint;
	fingerprint.string(treeDigest);
	fingerprint.string(range.line->sourceFile->uri);
	fingerprint.number(range.line->sourceFileLineIndex);
	fingerprint.number(range.line->mergedLineIndex);
	fingerprint.number(range.start());
	fingerprint.number(range.end());
	fingerprint.string(range.line->fullText);
	fingerprint.string(reference.pattern.text);
	fingerprint.number(static_cast<int>(reference.patternType));
	fingerprint.number(static_cast<int>(reference.purpose));
	fingerprint.number(reference.patternElements.size());
	for (const PatternElement &element : reference.patternElements) {
		fingerprint.number(static_cast<int>(element.type));
		fingerprint.string(element.text);
		fingerprint.number(element.startPos);
		fingerprint.boolean(element.isArgumentPack);
	}
	fingerprint.number(reference.pattern.keyframes.size());
	for (const auto &keyframe : reference.pattern.keyframes) {
		fingerprint.number(keyframe.patternPos);
		fingerprint.number(keyframe.linePos);
	}
	fingerprint.boolean(reference.expression != nullptr);
	if (reference.expression) {
		fingerprint.number(reference.expression->arguments.size());
		for (size_t index = 0; index < reference.expression->arguments.size(); index++) {
			std::optional<std::string> spelling = numericPatternArgumentSpelling(&reference, index);
			fingerprint.boolean(spelling.has_value());
			if (spelling)
				fingerprint.string(*spelling);
		}
	}
	fingerprint.boolean(reference.matchingScope != nullptr);
	for (const Section *scope = reference.matchingScope ? reference.matchingScope : range.section(); scope;
		 scope = scope->parent) {
		fingerprint.boolean(true);
		fingerprint.number(static_cast<int>(scope->type));
		fingerprint.boolean(scope->isValueExpressionContext());
		fingerprint.boolean(scope->openingLine != nullptr);
		if (scope->openingLine) {
			appendSourcePosition(fingerprint, Range(scope->openingLine, 0, 0));
			fingerprint.string(scope->openingLine->fullText);
		}
		std::vector<std::pair<std::string_view, const Range *>> declarations;
		declarations.reserve(scope->explicitVariableDeclarations.size());
		for (const auto &[name, declaration] : scope->explicitVariableDeclarations)
			declarations.emplace_back(name, &declaration);
		std::sort(declarations.begin(), declarations.end(), [](const auto &left, const auto &right) {
			return left.first < right.first;
		});
		fingerprint.number(declarations.size());
		for (const auto &[name, declaration] : declarations) {
			fingerprint.string(name);
			appendSourcePosition(fingerprint, *declaration);
		}
		std::vector<std::string_view> dataVariables;
		dataVariables.reserve(scope->dataVariableDeclarations.size());
		for (const std::string &name : scope->dataVariableDeclarations)
			dataVariables.push_back(name);
		std::sort(dataVariables.begin(), dataVariables.end());
		fingerprint.number(dataVariables.size());
		for (std::string_view name : dataVariables)
			fingerprint.string(name);
		Expression *header = scope->openingLine ? scope->openingLine->expression : nullptr;
		PatternMatch *headerMatch = header && header->patternReference ? header->patternReference->match
									: header						   ? header->patternMatch
																	   : nullptr;
		fingerprint.boolean(headerMatch != nullptr);
		if (headerMatch) {
			fingerprint.number(headerMatch->matchingDefinitions.size());
			for (const PatternDefinition *definition : headerMatch->matchingDefinitions)
				appendDefinition(fingerprint, definition);
		}
	}
	fingerprint.boolean(false);
	auto appendConfigDigest = [&](const std::string &path) {
		fingerprint.boolean(!path.empty());
		if (path.empty())
			return;
		auto cached = configDigests.find(path);
		if (cached == configDigests.end()) {
			lsp::SourceFile *config = context.fileSystem->getFile(path);
			cached = configDigests.emplace(path, config ? Json(sourceDigest(config)) : Json(nullptr)).first;
		}
		fingerprint.boolean(!cached->second.is_null());
		if (!cached->second.is_null())
			fingerprint.string(cached->second.get_ref<const std::string &>());
	};
	fingerprint.boolean(trackDependencies);
	appendConfigDigest(context.builtinSyntaxConfigPath);
	appendConfigDigest(context.projectSyntaxConfigPath);
	return fingerprint.finish();
}

Json IncrementalMatcherCache::nodePath(const PatternTreeNode *node) const {
	Json path = Json::array();
	for (const PatternTreeNode *current = node; current->parentNode; current = current->parentNode)
		path.push_back(Json::array({static_cast<int>(current->type), current->text, current->isArgumentPack}));
	std::reverse(path.begin(), path.end());
	const PatternTreeNode *root = node;
	while (root->parentNode)
		root = root->parentNode;
	for (size_t index = 0; index < std::size(context.patternTrees); index++)
		if (context.patternTrees[index] == root)
			return Json::array({index, path});
	crashCompilerBug("incremental matcher node does not belong to a pattern tree");
}

PatternTreeNode *IncrementalMatcherCache::restoreNode(const Json &path) const {
	PatternTreeNode *node = context.patternTrees[path.at(0).get<size_t>()];
	for (const Json &edge : path.at(1)) {
		PatternElement::Type type = static_cast<PatternElement::Type>(edge.at(0).get<int>());
		if (type == PatternElement::Type::Variable)
			node = edge.at(2).get<bool>() ? node->argumentPackChild : node->argumentChild;
		else {
			auto found = node->literalChildren.find(edge.at(1).get<std::string>());
			node = found == node->literalChildren.end() ? nullptr : found->second;
		}
		requireCompilerInvariant(node != nullptr, "cached matcher node missing from an identical pattern tree");
	}
	return node;
}

std::optional<Json> IncrementalMatcherCache::encodeMatch(const PatternReference &reference, const PatternMatch &match) const {
	Json definitions = Json::array();
	for (const PatternDefinition *definition : match.matchingDefinitions) {
		auto found = std::find(
			match.matchedEndNode->matchingDefinitions.begin(), match.matchedEndNode->matchingDefinitions.end(), definition
		);
		requireCompilerInvariant(
			found != match.matchedEndNode->matchingDefinitions.end(),
			"matcher result contains a definition outside its endpoint"
		);
		definitions.push_back(std::distance(match.matchedEndNode->matchingDefinitions.begin(), found));
	}
	Json nodes = Json::array();
	for (const PatternTreeNode *node : match.nodesPassed)
		nodes.push_back(nodePath(node));
	Json variables = Json::array();
	for (const VariableMatch &variable : match.discoveredVariables)
		variables.push_back(Json::array({variable.name, variable.lineStartPos, variable.lineEndPos}));
	Json literals = Json::array();
	for (const AcceptedLiteralMatch &literal : match.acceptedLiterals)
		literals.push_back(nodePath(literal.node));
	Json submatches = Json::array();
	for (const PatternMatch &submatch : match.subMatches) {
		std::optional<Json> encoded = encodeMatch(reference, submatch);
		if (!encoded)
			return std::nullopt;
		submatches.push_back(std::move(*encoded));
	}
	Json arguments = Json::array();
	for (const MatchedArgument &argument : match.orderedArguments) {
		int expressionIndex = -1;
		if (argument.expression) {
			if (!reference.expression)
				return std::nullopt;
			auto found =
				std::find(reference.expression->arguments.begin(), reference.expression->arguments.end(), argument.expression);
			if (found == reference.expression->arguments.end())
				return std::nullopt;
			expressionIndex = static_cast<int>(std::distance(reference.expression->arguments.begin(), found));
		}
		arguments.push_back(
			Json::array(
				{argument.argumentIndex, static_cast<int>(argument.kind), expressionIndex, argument.itemIndex,
				 argument.isPackElement}
			)
		);
	}
	return Json::array(
		{nodePath(match.matchedEndNode), definitions, match.lineStartPos, match.lineEndPos, nodes, variables, literals,
		 submatches, arguments}
	);
}

PatternMatch IncrementalMatcherCache::restoreMatch(const PatternReference &reference, const Json &encoded) const {
	PatternMatch match{};
	match.matchedEndNode = restoreNode(encoded.at(0));
	for (const Json &index : encoded.at(1))
		match.matchingDefinitions.push_back(match.matchedEndNode->matchingDefinitions.at(index.get<size_t>()));
	match.lineStartPos = encoded.at(2).get<size_t>();
	match.lineEndPos = encoded.at(3).get<size_t>();
	for (const Json &path : encoded.at(4))
		match.nodesPassed.push_back(restoreNode(path));
	for (const Json &variable : encoded.at(5))
		match.discoveredVariables.push_back(
			{variable.at(0).get<std::string>(), variable.at(1).get<size_t>(), variable.at(2).get<size_t>()}
		);
	for (const Json &path : encoded.at(6))
		match.acceptedLiterals.push_back({restoreNode(path)});
	for (const Json &submatch : encoded.at(7))
		match.subMatches.push_back(restoreMatch(reference, submatch));
	for (const Json &argument : encoded.at(8)) {
		int expressionIndex = argument.at(2).get<int>();
		Expression *expression = expressionIndex < 0 ? nullptr : reference.expression->arguments.at(expressionIndex);
		match.orderedArguments.push_back(
			{argument.at(0).get<size_t>(), static_cast<MatchedArgument::Kind>(argument.at(1).get<int>()), expression,
			 argument.at(3).get<size_t>(), argument.at(4).get<bool>()}
		);
	}
	return match;
}

std::optional<PatternMatch *>
IncrementalMatcherCache::reuse(PatternReference *reference, MatchOptions options, MatchDependencies *dependencies) {
	if (!available(options))
		return std::nullopt;
	std::string cacheKey = key(*reference, dependencies != nullptr);
	auto entry = previousEntries.find(cacheKey);
	if (entry == previousEntries.end())
		return std::nullopt;
	recorded[cacheKey] = entry->second;
	reusedCount++;
	Json value = Json::from_msgpack(entry->second.begin(), entry->second.end());
	if (value.contains("failure")) {
		requireCompilerInvariant(dependencies != nullptr, "cached failure is missing dependency storage");
		dependencies->clear();
		for (const Json &dependency : value.at("failure")) {
			PatternTreeNode *node = restoreNode(dependency.at(1));
			dependencies->push_back(
				{static_cast<MatchDependency::Kind>(dependency.at(0).get<int>()), node, node->endpointRevision,
				 dependency.at(2).get<std::string>()}
			);
		}
		return static_cast<PatternMatch *>(nullptr);
	}
	return new PatternMatch(restoreMatch(*reference, value.at("match")));
}

void IncrementalMatcherCache::record(
	PatternReference *reference, MatchOptions options, const PatternMatch *match, const MatchDependencies *dependencies
) {
	if (!available(options))
		return;
	if (match) {
		if (std::optional<Json> encoded = encodeMatch(*reference, *match)) {
			std::string packed;
			Json::to_msgpack(Json{{"match", std::move(*encoded)}}, packed);
			recorded[key(*reference, dependencies != nullptr)] = std::move(packed);
		}
		return;
	}
	if (!dependencies)
		return;
	Json encoded = Json::array();
	for (const MatchDependency &dependency : *dependencies)
		encoded.push_back(Json::array({static_cast<int>(dependency.kind), nodePath(dependency.node), dependency.literal}));
	std::string packed;
	Json::to_msgpack(Json{{"failure", std::move(encoded)}}, packed);
	recorded[key(*reference, true)] = std::move(packed);
}
