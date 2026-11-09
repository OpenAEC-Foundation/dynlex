#include "incrementalMatcherCache.h"
#include "compilerUtils.h"
#include "dataSections.h"
#include "lsp/sourceFile.h"
#include "parseContext.h"
#include "patternDefinition.h"
#include "patternReference.h"
#include "section.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/BLAKE3.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace {
using Json = IncrementalMatcherCache::Json;
constexpr std::string_view matcherCacheHeader = "DynLex matcher results v4\n";

void appendLength(std::string &output, std::uint64_t value) {
	do {
		unsigned char byte = value & 0x7f;
		value >>= 7;
		output.push_back(static_cast<char>(byte | (value ? 0x80 : 0)));
	} while (value);
}

std::uint64_t readLength(std::string_view input, size_t &position) {
	std::uint64_t value = 0;
	for (unsigned index = 0; index < 10; index++) {
		if (position == input.size())
			throw std::runtime_error("truncated incremental matcher cache length");
		unsigned char byte = input[position++];
		if (index == 9 && byte > 1)
			throw std::runtime_error("invalid incremental matcher cache length");
		value |= static_cast<std::uint64_t>(byte & 0x7f) << (index * 7);
		if (!(byte & 0x80))
			return value;
	}
	throw std::runtime_error("invalid incremental matcher cache length");
}

void appendString(std::string &output, std::string_view value) {
	appendLength(output, value.size());
	if (!value.empty())
		output.append(value.data(), value.size());
}

std::string_view readString(std::string_view input, size_t &position) {
	std::uint64_t length = readLength(input, position);
	if (length > input.size() - position)
		throw std::runtime_error("truncated incremental matcher cache string");
	std::string_view value = input.substr(position, static_cast<size_t>(length));
	position += static_cast<size_t>(length);
	return value;
}

std::string digest(std::string_view value) {
	llvm::BLAKE3 hash;
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
		key.string(opening->fullText);
		key.number(static_cast<int>(section->dataFieldOwner->type));
	}
}

void appendSourcePosition(Fingerprint &key, const Range &range) {
	requireCompilerInvariant(range.line && range.line->sourceFile, "incremental matcher source position has no source file");
	key.string(range.line->sourceFile->uri);
	key.number(range.start());
	key.number(range.end());
}

void appendDeclarationVisibility(Fingerprint &key, const Range &declaration, const Range &reference) {
	requireCompilerInvariant(declaration.line && reference.line, "incremental matcher declaration has no source line");
	int declarationLine = declaration.line->mergedLineIndex;
	int referenceLine = reference.line->mergedLineIndex;
	key.number(declarationLine < referenceLine ? 0 : declarationLine == referenceLine ? 1 : 2);
	if (declarationLine == referenceLine)
		key.number(declaration.start());
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

std::optional<std::string_view> IncrementalMatcherCache::resultKey(const PatternReference *reference) const {
	return reference->incrementalMatcherKey.empty() ? std::nullopt
													: std::optional<std::string_view>(reference->incrementalMatcherKey);
}

void IncrementalMatcherCache::reusePrecedenceSignature(std::string_view signature) {
	requireCompilerInvariant(context.patternPrecedenceRevision != 0, "precedence signature reuse needs resolved families");
	requireCompilerInvariant(
		cachedPrecedenceRevision != context.patternPrecedenceRevision || cachedPrecedenceDigest == signature,
		"cached precedence signature disagrees with the reusable signature"
	);
	cachedPrecedenceDigest = signature;
	cachedPrecedenceRevision = context.patternPrecedenceRevision;
}

std::string IncrementalMatcherCache::precedenceDigest() {
	requireCompilerInvariant(
		context.patternPrecedenceRevision != 0, "precedence match cache requires resolved syntax families"
	);
	if (cachedPrecedenceRevision == context.patternPrecedenceRevision)
		return cachedPrecedenceDigest;
	std::unordered_map<const PatternFamily *, size_t> familyIndices;
	familyIndices.reserve(context.patternFamilies.size());
	for (size_t index = 0; index < context.patternFamilies.size(); index++)
		familyIndices.emplace(context.patternFamilies[index].get(), index);
	std::vector<bool> participating(context.patternFamilies.size(), false);
	for (size_t index = 0; index < context.patternFamilies.size(); index++) {
		const PatternFamily &family = *context.patternFamilies[index];
		if (family.associativity != PatternAssociativity::Unspecified || !family.precedenceSuccessors.empty())
			participating[index] = true;
		for (const PatternFamily *successor : family.precedenceSuccessors)
			participating[familyIndices.at(successor)] = true;
	}
	std::vector<Fingerprint> memberFingerprints(context.patternFamilies.size());
	auto appendSections = [&](auto &&self, const Section *section) -> void {
		for (const PatternDefinition *definition : section->patternDefinitions) {
			size_t index = familyIndices.at(definition->family);
			if (participating[index])
				appendDefinition(memberFingerprints[index], definition);
		}
		for (const Section *child : section->children)
			self(self, child);
	};
	appendSections(appendSections, context.mainSection);
	std::vector<std::string> familyIds(context.patternFamilies.size());
	std::unordered_map<std::string, size_t> duplicateCounts;
	for (size_t index = 0; index < participating.size(); index++) {
		if (!participating[index])
			continue;
		std::string members = memberFingerprints[index].finish();
		familyIds[index] = members + ":" + std::to_string(duplicateCounts[members]++);
	}
	std::vector<std::string> records;
	for (size_t index = 0; index < participating.size(); index++) {
		if (!participating[index])
			continue;
		const PatternFamily &family = *context.patternFamilies[index];
		Fingerprint record;
		record.string(familyIds[index]);
		record.number(static_cast<int>(family.associativity));
		std::vector<std::string> successors;
		successors.reserve(family.precedenceSuccessors.size());
		for (const PatternFamily *successor : family.precedenceSuccessors)
			successors.push_back(familyIds[familyIndices.at(successor)]);
		std::sort(successors.begin(), successors.end());
		record.number(successors.size());
		for (const std::string &successor : successors)
			record.string(successor);
		records.push_back(record.finish());
	}
	std::sort(records.begin(), records.end());
	Fingerprint fingerprint;
	fingerprint.number(records.size());
	for (const std::string &record : records)
		fingerprint.string(record);
	cachedPrecedenceDigest = fingerprint.finish();
	cachedPrecedenceRevision = context.patternPrecedenceRevision;
	return cachedPrecedenceDigest;
}

bool IncrementalMatcherCache::available(MatchOptions options) {
	if (options.acceptLiterals || options.maxSteps ||
		context.compilationStage > ParseContext::CompilationStage::ResolvedPatternPrecedence)
		return false;
	for (const PatternTreeNode *root : context.patternTrees) {
		if (!root)
			return false;
		requireCompilerInvariant(root->mutationState != nullptr, "incremental matcher root has no mutation state");
	}
	return true;
}

std::string IncrementalMatcherCache::key(PatternReference &reference, bool trackDependencies) {
	auto started = std::chrono::steady_clock::now();
	const Range &range = reference.range();
	requireCompilerInvariant(range.line && range.line->sourceFile, "incremental matcher reference has no source");
	if (!reference.matcherKeyLiterals) {
		auto literals = std::make_unique<MatcherKeyLiterals>();
		std::vector<std::string_view> candidates;
		auto includeStartLiterals = [&](std::string_view text) {
			for (size_t runStart = 0; runStart < text.size();) {
				auto category = [](unsigned char character) {
					if (std::isalnum(character) || character == '_')
						return 0;
					return character == ' ' ? 1 : 2;
				};
				int runCategory = category(static_cast<unsigned char>(text[runStart]));
				size_t runEnd = runStart + 1;
				while (runEnd < text.size() && category(static_cast<unsigned char>(text[runEnd])) == runCategory)
					runEnd++;
				if (runCategory == 0) {
					// Word elements are atomic; only Other elements can split during matching.
					candidates.push_back(text.substr(runStart, runEnd - runStart));
				} else {
					for (size_t start = runStart; start < runEnd; start++)
						for (size_t end = start + 1; end <= runEnd; end++)
							candidates.push_back(text.substr(start, end - start));
				}
				runStart = runEnd;
			}
		};
		includeStartLiterals(range.line->fullText);
		includeStartLiterals(reference.pattern.text);
		if (reference.expression) {
			literals->numericSpellings.reserve(reference.expression->arguments.size());
			for (size_t index = 0; index < reference.expression->arguments.size(); index++) {
				literals->numericSpellings.push_back(numericPatternArgumentSpelling(&reference, index));
				if (literals->numericSpellings.back())
					includeStartLiterals(*literals->numericSpellings.back());
			}
		}
		std::sort(candidates.begin(), candidates.end());
		candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
		literals->possibleStartLiterals.reserve(candidates.size());
		for (std::string_view candidate : candidates)
			literals->possibleStartLiterals.emplace_back(candidate);
		reference.matcherKeyLiterals = std::move(literals);
	}
	const MatcherKeyLiterals &literals = *reference.matcherKeyLiterals;
	Fingerprint fingerprint;
	bool precedencePhase = context.compilationStage == ParseContext::CompilationStage::ResolvedPatternPrecedence;
	fingerprint.boolean(precedencePhase);
	if (precedencePhase)
		fingerprint.string(precedenceDigest());
	auto appendTreeState = [&](const PatternTreeNode *root) {
		fingerprint.string(root->mutationState->wildcardDigest);
		for (const std::string &literal : literals.possibleStartLiterals) {
			auto found = root->mutationState->literalDigests.find(literal);
			if (found == root->mutationState->literalDigests.end())
				continue;
			fingerprint.string(literal);
			fingerprint.string(found->second);
		}
	};
	const PatternTreeNode *root = context.patternTrees[static_cast<size_t>(reference.patternType)];
	const PatternTreeNode *functions = context.patternTrees[static_cast<size_t>(SectionType::Function)];
	appendTreeState(root);
	// Nested expression matches and named single-word probes use the function tree.
	if (functions != root)
		appendTreeState(functions);
	fingerprint.string(range.line->sourceFile->uri);
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
		fingerprint.number(literals.numericSpellings.size());
		for (const std::optional<std::string> &spelling : literals.numericSpellings) {
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
			appendDeclarationVisibility(fingerprint, *declaration, range);
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
	auto appendConfigIdentity = [&](const std::string &path, const SyntaxConfig &syntax) {
		fingerprint.boolean(!path.empty());
		fingerprint.string(syntax.nonMessageIdentity);
	};
	fingerprint.boolean(trackDependencies);
	appendConfigIdentity(context.builtinSyntaxConfigPath, context.builtinSyntax);
	appendConfigIdentity(context.projectSyntaxConfigPath, context.projectSyntax);
	std::string result = fingerprint.finish();
	keyMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
	return result;
}

void IncrementalMatcherCache::appendNode(std::string &output, const PatternTreeNode *node) const {
	std::vector<const PatternTreeNode *> edges;
	for (const PatternTreeNode *current = node; current->parentNode; current = current->parentNode)
		edges.push_back(current);
	const PatternTreeNode *root = node;
	while (root->parentNode)
		root = root->parentNode;
	size_t rootIndex = std::size(context.patternTrees);
	for (size_t index = 0; index < std::size(context.patternTrees); index++)
		if (context.patternTrees[index] == root) {
			rootIndex = index;
			break;
		}
	requireCompilerInvariant(rootIndex < std::size(context.patternTrees), "incremental matcher node is outside its tree");
	appendLength(output, rootIndex);
	appendLength(output, edges.size());
	for (auto edge = edges.rbegin(); edge != edges.rend(); edge++) {
		appendLength(output, static_cast<int>((*edge)->type));
		appendString(output, (*edge)->text);
		appendLength(output, (*edge)->isArgumentPack);
	}
}

PatternTreeNode *IncrementalMatcherCache::restoreNode(std::string_view input, size_t &position) const {
	std::uint64_t rootIndex = readLength(input, position);
	requireCompilerInvariant(rootIndex < std::size(context.patternTrees), "cached matcher has an invalid tree index");
	PatternTreeNode *node = context.patternTrees[rootIndex];
	std::uint64_t edgeCount = readLength(input, position);
	for (std::uint64_t index = 0; index < edgeCount; index++) {
		auto type = static_cast<PatternElement::Type>(readLength(input, position));
		std::string_view text = readString(input, position);
		bool isArgumentPack = readLength(input, position) != 0;
		if (type == PatternElement::Type::Variable)
			node = isArgumentPack ? node->argumentPackChild : node->argumentChild;
		else {
			auto found = node->literalChildren.find(text);
			node = found == node->literalChildren.end() ? nullptr : found->second;
		}
		requireCompilerInvariant(node != nullptr, "cached matcher node missing from an identical pattern tree");
	}
	return node;
}

bool IncrementalMatcherCache::appendMatch(
	std::string &output, const PatternReference &reference, const PatternMatch &match
) const {
	appendNode(output, match.matchedEndNode);
	appendLength(output, match.matchingDefinitions.size());
	for (const PatternDefinition *definition : match.matchingDefinitions) {
		auto found = std::find(
			match.matchedEndNode->matchingDefinitions.begin(), match.matchedEndNode->matchingDefinitions.end(), definition
		);
		requireCompilerInvariant(
			found != match.matchedEndNode->matchingDefinitions.end(),
			"matcher result contains a definition outside its endpoint"
		);
		appendLength(output, std::distance(match.matchedEndNode->matchingDefinitions.begin(), found));
	}
	appendLength(output, match.lineStartPos);
	appendLength(output, match.lineEndPos);
	appendLength(output, match.nodesPassed.size());
	for (const PatternTreeNode *node : match.nodesPassed)
		appendNode(output, node);
	appendLength(output, match.discoveredVariables.size());
	for (const VariableMatch &variable : match.discoveredVariables) {
		appendString(output, variable.name);
		appendLength(output, variable.lineStartPos);
		appendLength(output, variable.lineEndPos);
	}
	appendLength(output, match.acceptedLiterals.size());
	for (const AcceptedLiteralMatch &literal : match.acceptedLiterals)
		appendNode(output, literal.node);
	appendLength(output, match.subMatches.size());
	for (const PatternMatch &submatch : match.subMatches)
		if (!appendMatch(output, reference, submatch))
			return false;
	appendLength(output, match.orderedArguments.size());
	for (const MatchedArgument &argument : match.orderedArguments) {
		appendLength(output, argument.argumentIndex);
		appendLength(output, static_cast<int>(argument.kind));
		appendLength(output, argument.expression != nullptr);
		if (argument.expression) {
			if (!reference.expression)
				return false;
			auto found =
				std::find(reference.expression->arguments.begin(), reference.expression->arguments.end(), argument.expression);
			if (found == reference.expression->arguments.end())
				return false;
			appendLength(output, std::distance(reference.expression->arguments.begin(), found));
		}
		appendLength(output, argument.itemIndex);
		appendLength(output, argument.isPackElement);
	}
	return true;
}

PatternMatch
IncrementalMatcherCache::restoreMatch(std::string_view input, size_t &position, const PatternReference &reference) const {
	PatternMatch match{};
	match.matchedEndNode = restoreNode(input, position);
	std::uint64_t definitionCount = readLength(input, position);
	for (std::uint64_t index = 0; index < definitionCount; index++)
		match.matchingDefinitions.push_back(match.matchedEndNode->matchingDefinitions.at(readLength(input, position)));
	match.lineStartPos = readLength(input, position);
	match.lineEndPos = readLength(input, position);
	std::uint64_t nodeCount = readLength(input, position);
	for (std::uint64_t index = 0; index < nodeCount; index++)
		match.nodesPassed.push_back(restoreNode(input, position));
	std::uint64_t variableCount = readLength(input, position);
	for (std::uint64_t index = 0; index < variableCount; index++) {
		std::string name(readString(input, position));
		size_t start = readLength(input, position);
		size_t end = readLength(input, position);
		match.discoveredVariables.push_back({std::move(name), start, end});
	}
	std::uint64_t literalCount = readLength(input, position);
	for (std::uint64_t index = 0; index < literalCount; index++)
		match.acceptedLiterals.push_back({restoreNode(input, position)});
	std::uint64_t submatchCount = readLength(input, position);
	for (std::uint64_t index = 0; index < submatchCount; index++)
		match.subMatches.push_back(restoreMatch(input, position, reference));
	std::uint64_t argumentCount = readLength(input, position);
	for (std::uint64_t index = 0; index < argumentCount; index++) {
		size_t argumentIndex = readLength(input, position);
		auto kind = static_cast<MatchedArgument::Kind>(readLength(input, position));
		Expression *expression = nullptr;
		if (readLength(input, position) != 0) {
			requireCompilerInvariant(reference.expression != nullptr, "cached matcher argument has no source expression");
			expression = reference.expression->arguments.at(readLength(input, position));
		}
		size_t itemIndex = readLength(input, position);
		bool isPackElement = readLength(input, position) != 0;
		match.orderedArguments.push_back({argumentIndex, kind, expression, itemIndex, isPackElement});
	}
	return match;
}

std::optional<PatternMatch *>
IncrementalMatcherCache::reuse(PatternReference *reference, MatchOptions options, MatchDependencies *dependencies) {
	if (!available(options)) {
		reference->incrementalMatcherKey.clear();
		reference->prevalidatedMatcherKey.clear();
		return std::nullopt;
	}
	bool prevalidated = !reference->prevalidatedMatcherKey.empty();
	std::string cacheKey = prevalidated ? std::exchange(reference->prevalidatedMatcherKey, std::string{})
										: key(*reference, dependencies != nullptr);
	auto entry = previousEntries.find(cacheKey);
	if (entry == previousEntries.end() && prevalidated) {
		cacheKey = key(*reference, dependencies != nullptr);
		entry = previousEntries.find(cacheKey);
		prevalidated = false;
	}
	if (entry == previousEntries.end()) {
		reference->incrementalMatcherKey.clear();
		return std::nullopt;
	}
	std::string_view value = entry->second;
	if (value.empty())
		throw std::runtime_error("empty incremental matcher cache value");
	size_t position = 1;
	if (value[0] == 0) {
		reference->incrementalMatcherKey.clear();
		requireCompilerInvariant(dependencies != nullptr, "cached failure is missing dependency storage");
		dependencies->clear();
		std::uint64_t count = readLength(value, position);
		for (std::uint64_t index = 0; index < count; index++) {
			auto kind = static_cast<MatchDependency::Kind>(readLength(value, position));
			PatternTreeNode *node = restoreNode(value, position);
			std::string literal(readString(value, position));
			dependencies->push_back({kind, node, node->endpointRevision, std::move(literal)});
		}
		if (position != value.size())
			throw std::runtime_error("trailing incremental matcher cache data");
		recorded[cacheKey] = value;
		reusedCount++;
		prevalidatedCount += prevalidated;
		if (prevalidated && context.compilationStage == ParseContext::CompilationStage::ResolvedPatternPrecedence)
			prevalidatedPrecedenceCount++;
		if (context.compilationStage == ParseContext::CompilationStage::ResolvedPatternPrecedence)
			reusedPrecedenceCount++;
		return static_cast<PatternMatch *>(nullptr);
	}
	if (value[0] != 1)
		throw std::runtime_error("invalid incremental matcher cache value kind");
	auto restoreStarted = std::chrono::steady_clock::now();
	PatternMatch match = restoreMatch(value, position, *reference);
	restoreMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - restoreStarted).count();
	if (position != value.size())
		throw std::runtime_error("trailing incremental matcher cache data");
	recorded[cacheKey] = value;
	reference->incrementalMatcherKey = cacheKey;
	reusedCount++;
	prevalidatedCount += prevalidated;
	if (prevalidated && context.compilationStage == ParseContext::CompilationStage::ResolvedPatternPrecedence)
		prevalidatedPrecedenceCount++;
	if (context.compilationStage == ParseContext::CompilationStage::ResolvedPatternPrecedence)
		reusedPrecedenceCount++;
	return new PatternMatch(std::move(match));
}

void IncrementalMatcherCache::record(
	PatternReference *reference, MatchOptions options, const PatternMatch *match, const MatchDependencies *dependencies
) {
	if (!available(options)) {
		reference->incrementalMatcherKey.clear();
		return;
	}
	if (match) {
		std::string packed(1, '\1');
		if (appendMatch(packed, *reference, *match)) {
			std::string cacheKey = key(*reference, dependencies != nullptr);
			reference->incrementalMatcherKey = cacheKey;
			recorded[std::move(cacheKey)] = std::move(packed);
		} else
			reference->incrementalMatcherKey.clear();
		return;
	}
	reference->incrementalMatcherKey.clear();
	if (!dependencies)
		return;
	std::string packed(1, '\0');
	appendLength(packed, dependencies->size());
	for (const MatchDependency &dependency : *dependencies) {
		appendLength(packed, static_cast<int>(dependency.kind));
		appendNode(packed, dependency.node);
		appendString(packed, dependency.literal);
	}
	recorded[key(*reference, true)] = std::move(packed);
}
