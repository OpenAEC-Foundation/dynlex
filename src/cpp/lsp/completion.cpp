#include "completion.h"
#include "expression.h"
#include "parseContext.h"
#include "pathUtils.h"
#include "pattern/patternDefinition.h"
#include "pattern/patternReference.h"
#include "pattern/pattern_tree/matchProgress.h"
#include "pattern/pattern_tree/patternElement.h"
#include "patternTreeNode.h"
#include "syntaxConfig.h"
#include "variable.h"
#include <algorithm>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>

using namespace std::literals;

namespace lsp {

namespace {

std::string trimLeft(std::string_view text) {
	size_t index = 0;
	while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index]))) {
		index++;
	}
	return std::string(text.substr(index));
}

std::string normalizeCompletionWhitespace(std::string_view text) {
	std::string normalized;
	normalized.reserve(text.size());

	bool pendingSpace = false;
	for (char c : text) {
		if (std::isspace(static_cast<unsigned char>(c))) {
			if (!normalized.empty()) {
				pendingSpace = true;
			}
			continue;
		}
		if (pendingSpace) {
			normalized += ' ';
			pendingSpace = false;
		}
		normalized += c;
	}
	if (pendingSpace) {
		normalized += ' ';
	}
	return normalized;
}

bool startsWithImportKeyword(std::string_view prefix, std::string_view importKeyword) {
	std::string_view trimmed = prefix;
	while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) {
		trimmed.remove_prefix(1);
	}
	return extractDirectiveArgument(trimmed, importKeyword).has_value();
}

bool hasCompilationStage(const ParseContext *context, ParseContext::CompilationStage stage) {
	return context && context->hasCompleted(stage);
}

bool startsWith(std::string_view text, std::string_view prefix) { return text.substr(0, prefix.size()) == prefix; }

Range makeRange(int line, int startCharacter, int endCharacter) {
	Range range;
	range.start.line = line;
	range.start.character = startCharacter;
	range.end.line = line;
	range.end.character = endCharacter;
	return range;
}

std::string normalizeCompletionPatternPrefix(std::string_view prefix) {
	std::string normalized;
	normalized.reserve(prefix.size());

	for (size_t i = 0; i < prefix.size();) {
		char c = prefix[i];

		if (c == '"') {
			size_t j = i + 1;
			bool escaped = false;
			for (; j < prefix.size(); ++j) {
				if (!escaped && prefix[j] == '"') {
					normalized += argumentChar;
					i = j + 1;
					goto next_char;
				}
				escaped = (!escaped && prefix[j] == '\\');
				if (prefix[j] != '\\')
					escaped = false;
			}
			normalized += argumentChar;
			break;
		}

		if (c == '{') {
			size_t close = prefix.find('}', i + 1);
			normalized += argumentChar;
			if (close == std::string_view::npos)
				break;
			i = close + 1;
			continue;
		}

		if (c == '(') {
			size_t depth = 1;
			size_t j = i + 1;
			bool inString = false;
			bool escaped = false;
			for (; j < prefix.size(); ++j) {
				char nested = prefix[j];
				if (inString) {
					if (!escaped && nested == '"') {
						inString = false;
					}
					escaped = (!escaped && nested == '\\');
					if (nested != '\\')
						escaped = false;
					continue;
				}
				if (nested == '"') {
					inString = true;
					continue;
				}
				if (nested == '(') {
					depth++;
				} else if (nested == ')') {
					depth--;
					if (depth == 0) {
						normalized += argumentChar;
						i = j + 1;
						goto next_char;
					}
				}
			}
			normalized += argumentChar;
			break;
		}

		if (std::isdigit(static_cast<unsigned char>(c))) {
			bool startsNumber = i == 0 || !std::isalnum(static_cast<unsigned char>(prefix[i - 1]));
			if (startsNumber) {
				size_t j = i + 1;
				bool seenDot = false;
				while (j < prefix.size()) {
					char digit = prefix[j];
					if (std::isdigit(static_cast<unsigned char>(digit))) {
						j++;
						continue;
					}
					if (digit == '.' && !seenDot) {
						seenDot = true;
						j++;
						continue;
					}
					break;
				}
				bool endsNumber = j >= prefix.size() || !std::isalnum(static_cast<unsigned char>(prefix[j]));
				if (endsNumber) {
					normalized += argumentChar;
					i = j;
					continue;
				}
			}
		}

		normalized += c;
		i++;
	next_char:;
	}

	return normalized;
}

SourceFile *completionSourceFile(const CompletionContext &context) {
	requireCompilerInvariant(context.parseContext != nullptr, "pattern completion requires a parse context");
	const std::string requestedUri = pathutil::toAbsoluteUri(context.uri);
	for (const auto &[ignoredPath, sourceFile] : context.parseContext->importedFiles) {
		(void)ignoredPath;
		requireCompilerInvariant(sourceFile != nullptr, "imported source map contains a null source file");
		if (pathutil::toAbsoluteUri(sourceFile->uri) == requestedUri)
			return sourceFile;
	}
	crashCompilerBug("completion source file is missing from the imported source map");
}

bool subtreeHasVisibleDefinition(PatternTreeNode *node, const SourceFile &sourceFile) {
	if (!node)
		return false;
	std::vector<PatternTreeNode *> pending = {node};
	std::set<PatternTreeNode *> visited;
	while (!pending.empty()) {
		PatternTreeNode *current = pending.back();
		pending.pop_back();
		if (!current || !visited.insert(current).second)
			continue;
		for (PatternDefinition *definition : current->matchingDefinitions) {
			requireCompilerInvariant(definition != nullptr, "pattern tree endpoint contains a null definition");
			if (isPatternDefinitionVisibleFromSource(*definition, sourceFile))
				return true;
		}
		for (const auto &[ignoredLiteral, child] : current->literalChildren) {
			(void)ignoredLiteral;
			pending.push_back(child);
		}
		pending.push_back(current->argumentChild);
	}
	return false;
}

bool nodeHasVisibleDefinition(PatternTreeNode *node, const SourceFile &sourceFile) {
	if (!node)
		return false;
	return std::any_of(
		node->matchingDefinitions.begin(), node->matchingDefinitions.end(),
		[&](const PatternDefinition *definition) {
		requireCompilerInvariant(definition != nullptr, "pattern tree endpoint contains a null definition");
		return isPatternDefinitionVisibleFromSource(*definition, sourceFile);
	}
	);
}

std::set<std::string> visibleVariableNames(const CompletionContext &context) {
	SourceFile *sourceFile = completionSourceFile(context);
	Section *scope =
		context.logicalLine && context.logicalLine->section ? context.logicalLine->section : context.parseContext->mainSection;

	std::set<std::string> names;
	for (Section *section = scope; section; section = section->parent) {
		for (const auto &[name, variable] : section->variables) {
			requireCompilerInvariant(variable != nullptr, "completion scope contains a null variable");
			requireCompilerInvariant(variable->definition != nullptr, "completion variable has no definition");
			requireCompilerInvariant(variable->definition->range.line != nullptr, "completion variable definition has no line");
			SourceLocation definition = variable->definition->range.sourceStart();
			if (definition.sourceFile == sourceFile &&
				(definition.sourceFileLineIndex > context.line ||
				 (definition.sourceFileLineIndex == context.line && definition.column >= context.character)))
				continue;
			names.insert(name);
		}
	}
	return names;
}

void addCompletionItem(
	std::vector<CompletionItem> &items, std::set<std::string> &seen, std::string label, CompletionItemKind kind,
	std::string detail = {}, std::string insertText = {}, std::string sortText = {}, std::optional<TextEdit> textEdit = {}
) {
	if (label.empty() || !seen.insert(label).second) {
		return;
	}

	CompletionItem item;
	item.label = label;
	item.kind = kind;
	if (!detail.empty()) {
		item.detail = std::move(detail);
	}
	if (!insertText.empty()) {
		item.insertText = std::move(insertText);
	}
	if (!sortText.empty()) {
		item.sortText = std::move(sortText);
	}
	if (textEdit) {
		item.textEdit = std::move(textEdit);
	}
	items.push_back(std::move(item));
}

std::string extendLiteralSuggestion(std::string literal, PatternTreeNode *node) {
	PatternTreeNode *current = node;
	while (current && current->literalChildren.size() == 1 && !current->argumentChild && current->matchingDefinitions.empty() &&
		   literal.size() < 64) {
		const auto &[nextLiteral, nextNode] = *current->literalChildren.begin();
		literal += nextLiteral;
		current = nextNode;
	}
	return literal;
}

void collectNextLiteralSuggestions(
	PatternTreeNode *node, std::vector<CompletionItem> &items, std::set<std::string> &seen, std::string_view detailPrefix,
	std::string_view sortPrefix, const SourceFile &sourceFile, const TextEdit &textEdit
) {
	std::vector<std::string> literals;
	if (node) {
		for (const auto &[text, child] : node->literalChildren) {
			if (!subtreeHasVisibleDefinition(child, sourceFile))
				continue;
			if (text == " " && child && !child->literalChildren.empty()) {
				for (const auto &[nextText, nextChild] : child->literalChildren) {
					if (!subtreeHasVisibleDefinition(nextChild, sourceFile))
						continue;
					literals.push_back(extendLiteralSuggestion(text + nextText, nextChild));
				}
				continue;
			}
			literals.push_back(extendLiteralSuggestion(text, child));
		}
	}
	std::sort(literals.begin(), literals.end());
	for (const std::string &literal : literals) {
		TextEdit literalEdit = textEdit;
		literalEdit.newText = literal;
		addCompletionItem(
			items, seen, literal, CompletionItemKind::Keyword, std::string(detailPrefix), literal,
			std::string(sortPrefix) + literal, std::move(literalEdit)
		);
	}
}

bool nodeAcceptsArgument(PatternTreeNode *node, const SourceFile &sourceFile) {
	return node && node->argumentChild && subtreeHasVisibleDefinition(node->argumentChild, sourceFile);
}

void collectVariableSuggestions(
	PatternTreeNode *node, const std::set<std::string> &variableNames, std::vector<CompletionItem> &items,
	std::set<std::string> &seen, const SourceFile &sourceFile, const CompletionContext &context, std::string_view partial = {},
	std::optional<size_t> replacementLength = std::nullopt
) {
	if (!nodeAcceptsArgument(node, sourceFile))
		return;
	for (const std::string &name : variableNames) {
		if (!name.starts_with(partial))
			continue;
		TextEdit textEdit;
		size_t replacedCharacters = replacementLength.value_or(partial.size());
		textEdit.range = makeRange(context.line, context.character - static_cast<int>(replacedCharacters), context.character);
		textEdit.newText = name;
		addCompletionItem(items, seen, name, CompletionItemKind::Variable, "variable", name, "2_" + name, std::move(textEdit));
	}
}

std::set<std::string> directMatchedVariables(const MatchProgress &progress, const MatchStorage &storage) {
	std::set<std::string> names;
	size_t index = progress.match.discoveredVariables.last;
	while (index != noMatchSequenceNode) {
		requireCompilerInvariant(index < storage.matchedVariables.size(), "completion matcher variable sequence is invalid");
		const MatchSequenceNode<VariableMatch> &match = storage.matchedVariables[index];
		names.insert(match.value.name);
		index = match.previous;
	}
	return names;
}

struct MatcherFrontier {
	PatternTreeNode *node{};
	std::set<std::string> acceptedVariables;
};

struct MatcherFrontierCandidate {
	MatcherFrontier frontier;
	size_t variableCount{};
	size_t parentDepth{};
	size_t completedSubmatchCount{};
};

std::vector<MatcherFrontierCandidate>
collectMatcherFrontierCandidates(const CompletionContext &context, SectionType sectionType, const std::string &linePrefix) {
	std::vector<MatcherFrontierCandidate> candidates;
	if (!context.parseContext || !context.parseContext->patternTrees[(int)sectionType])
		return candidates;

	std::string normalizedPrefix = normalizeCompletionPatternPrefix(linePrefix);
	Expression expr;
	expr.range.subString = normalizedPrefix;
	CodeLine syntheticLine(std::string_view{}, completionSourceFile(context));
	expr.range.line = &syntheticLine;
	PatternReference reference(&expr, sectionType);
	reference.patternElements = getPatternElements(reference.pattern.text);
	std::deque<Expression> syntheticArguments;
	for (const PatternElement &element : reference.patternElements) {
		if (element.type != PatternElement::Type::Variable)
			continue;
		Expression &argument = syntheticArguments.emplace_back();
		argument.kind = Expression::Kind::TypedPlaceholder;
		argument.range.line = &syntheticLine;
		expr.arguments.push_back(&argument);
	}

	MatchStorage storage;
	std::vector<MatchProgress> queue = {MatchProgress(context.parseContext, &reference)};
	std::unordered_map<MatchControlState, MatchParentAlternatives *, MatchControlStateHash> memoizedStates;
	while (!queue.empty()) {
		MatchProgress current = std::move(queue.back());
		queue.pop_back();

		auto [memoizedState, inserted] = memoizedStates.try_emplace(current.controlState(), current.parents);
		if (!inserted) {
			std::vector<MatchProgress> resumedProgresses;
			MatchParentAlternatives *canonicalParents = memoizedState->second;
			MatchParentAlternatives *incomingParents = current.parents;
			if (canonicalParents && incomingParents && canonicalParents != incomingParents) {
				std::vector<const MatchProgress *> addedParents;
				for (const MatchProgress *incomingParent : incomingParents->values) {
					if (canonicalParents->addParent(incomingParent))
						addedParents.push_back(incomingParent);
				}
				for (auto addedParent = addedParents.rbegin(); addedParent != addedParents.rend(); addedParent++) {
					for (auto completion = canonicalParents->completedSubmatches.rbegin();
						 completion != canonicalParents->completedSubmatches.rend(); completion++) {
						resumedProgresses.push_back(MatchProgress::resumeParent(storage, **addedParent, *completion));
					}
				}
			}
			queue.insert(
				queue.end(), std::make_move_iterator(resumedProgresses.begin()),
				std::make_move_iterator(resumedProgresses.end())
			);
			continue;
		}

		std::vector<PatternDefinition *> visibleDefinitions = current.visibleDefinitions();
		MatchStep matchStep = current.step(storage, visibleDefinitions);
		if (current.isSubmatchComplete(visibleDefinitions)) {
			requireCompilerInvariant(matchStep.hasCompletedSubmatch, "completion matcher lost completed submatch data");
			current.parents->addCompletion(std::move(matchStep.completedSubmatch));
		}

		bool sourceComplete = current.sourceElementIndex == reference.patternElements.size();
		if (sourceComplete && matchStep.nextMatches.empty() &&
			subtreeHasVisibleDefinition(current.currentNode, *completionSourceFile(context))) {
			size_t parentDepth = 0;
			for (const MatchProgress *nested = &current; nested->parents;) {
				requireCompilerInvariant(
					!nested->parents->values.empty(), "completion matcher has a parent set without a parent"
				);
				nested = nested->parents->values.front();
				parentDepth++;
			}
			candidates.push_back({
				{current.currentNode, directMatchedVariables(current, storage)},
				current.match.discoveredVariables.size(),
				parentDepth,
				current.match.subMatches.size(),
			});
		}
		queue.insert(
			queue.end(), std::make_move_iterator(matchStep.nextMatches.begin()),
			std::make_move_iterator(matchStep.nextMatches.end())
		);
	}
	return candidates;
}

std::optional<MatcherFrontier>
collectMatcherFrontier(const CompletionContext &context, SectionType sectionType, const std::string &linePrefix) {
	std::vector<MatcherFrontierCandidate> candidates = collectMatcherFrontierCandidates(context, sectionType, linePrefix);
	if (candidates.empty())
		return std::nullopt;
	const MatcherFrontierCandidate &best = *std::min_element(
		candidates.begin(), candidates.end(),
		[](const MatcherFrontierCandidate &left, const MatcherFrontierCandidate &right) {
		return std::tie(left.variableCount, left.parentDepth, left.completedSubmatchCount) <
			   std::tie(right.variableCount, right.parentDepth, right.completedSubmatchCount);
	}
	);
	return best.frontier;
}

std::string pathToSlashString(const std::filesystem::path &path) { return path.generic_string(); }

void addImportPathCandidate(
	std::set<std::string> &candidates, const std::filesystem::path &candidatePath, const std::filesystem::path &currentDir,
	const std::filesystem::path &workspaceRoot
) {
	std::error_code ec;
	std::filesystem::path normalized = std::filesystem::weakly_canonical(candidatePath, ec);
	if (ec) {
		normalized = candidatePath.lexically_normal();
	}

	if (!workspaceRoot.empty()) {
		std::filesystem::path libRoot = workspaceRoot / "lib";
		ec.clear();
		std::filesystem::path relativeToLib = std::filesystem::relative(normalized, libRoot, ec);
		if (!ec && !relativeToLib.empty() && !startsWith(pathToSlashString(relativeToLib), "..")) {
			candidates.insert(pathToSlashString(std::filesystem::path("lib") / relativeToLib));
		}
	}

	ec.clear();
	std::filesystem::path relativeToCurrent = std::filesystem::relative(normalized, currentDir, ec);
	if (!ec && !relativeToCurrent.empty() && !startsWith(pathToSlashString(relativeToCurrent), "..")) {
		candidates.insert(pathToSlashString(relativeToCurrent));
	}

	ec.clear();
	std::filesystem::path relativeToWorkspace = std::filesystem::relative(normalized, workspaceRoot, ec);
	if (!ec && !relativeToWorkspace.empty() && !startsWith(pathToSlashString(relativeToWorkspace), "..")) {
		candidates.insert(pathToSlashString(relativeToWorkspace));
	}
}

CompletionList collectImportPathCompletions(const CompletionContext &context) {
	CompletionList result;
	std::vector<CompletionItem> items;
	std::set<std::string> seenLabels;
	std::set<std::string> candidates;

	std::string trimmed = trimLeft(context.linePrefix);
	const SyntaxConfig &syntax =
		context.parseContext ? syntaxConfigForSourcePath(*context.parseContext, context.uri) : SyntaxConfig{};
	std::optional<std::string_view> requestedPrefixView = extractDirectiveArgument(trimmed, syntax.importKeyword);
	std::string requestedPrefix = requestedPrefixView ? std::string(*requestedPrefixView) : "";
	std::filesystem::path currentFile = pathutil::toFilesystemPath(context.uri);
	std::filesystem::path currentDir = currentFile.parent_path();
	std::filesystem::path workspaceRoot =
		context.workspaceRootPath.empty() ? std::filesystem::current_path() : std::filesystem::path(context.workspaceRootPath);

	if (context.parseContext) {
		for (const auto &[path, sourceFile] : context.parseContext->importedFiles) {
			std::filesystem::path importedPath =
				pathutil::toFilesystemPath(sourceFile && !sourceFile->uri.empty() ? sourceFile->uri : path);
			addImportPathCandidate(candidates, importedPath, currentDir, workspaceRoot);
		}
	}

	std::error_code ec;
	if (std::filesystem::exists(workspaceRoot, ec)) {
		for (std::filesystem::recursive_directory_iterator it(workspaceRoot, ec), end; it != end; it.increment(ec)) {
			if (ec) {
				ec.clear();
				continue;
			}
			if (!it->is_regular_file() || it->path().extension() != ".dl") {
				continue;
			}

			addImportPathCandidate(candidates, it->path(), currentDir, workspaceRoot);

			std::ifstream file(it->path());
			std::string fileLine;
			while (std::getline(file, fileLine)) {
				std::string trimmedLine = trimLeft(fileLine);
				if (std::optional<std::string_view> importTarget =
						extractDirectiveArgument(trimmedLine, syntax.importKeyword)) {
					candidates.insert(std::string(*importTarget));
				}
			}
		}
	}

	for (const std::string &candidate : candidates) {
		if (!requestedPrefix.empty() && !candidate.starts_with(requestedPrefix)) {
			continue;
		}
		TextEdit textEdit;
		textEdit.range =
			makeRange(context.line, context.character - static_cast<int>(requestedPrefix.size()), context.character);
		textEdit.newText = candidate;
		addCompletionItem(
			items, seenLabels, candidate, CompletionItemKind::File, "import path", candidate, "0_" + candidate, textEdit
		);
	}

	result.items = std::move(items);
	return result;
}

void addKeywordCompletions(
	std::vector<CompletionItem> &items, std::set<std::string> &seenLabels, const CompletionContext &context
) {
	std::string trimmed = trimLeft(context.linePrefix);
	if (context.linePrefix.size() != trimmed.size()) {
		return;
	}

	const SyntaxConfig &syntax =
		context.parseContext ? syntaxConfigForSourcePath(*context.parseContext, context.uri) : SyntaxConfig{};
	const std::vector<std::pair<std::string, std::string>> keywordSuggestions = {
		{syntax.importKeyword + " ", "import another file"},
		{syntax.functionName + " ", "define a function"},
		{syntax.conversionName + " ", "define an explicit conversion"},
		{syntax.implicitName + " " + syntax.conversionName + " ", "define an implicit conversion"},
		{syntax.sectionName + " ", "define a section"},
		{syntax.className + " ", "define a class"},
		{syntax.flexName + " " + syntax.functionName + " ", "define a flex"},
		{syntax.localName + " " + syntax.functionName + " ", "define a local function"},
	};

	for (const auto &[keyword, detail] : keywordSuggestions) {
		if (trimmed.empty()) {
			TextEdit textEdit;
			textEdit.range = makeRange(context.line, context.character, context.character);
			textEdit.newText = keyword;
			addCompletionItem(
				items, seenLabels, keyword, CompletionItemKind::Keyword, detail, keyword, "0_" + keyword, textEdit
			);
			continue;
		}
		if (keyword.starts_with(trimmed)) {
			TextEdit textEdit;
			textEdit.range = makeRange(context.line, context.character - static_cast<int>(trimmed.size()), context.character);
			textEdit.newText = keyword;
			addCompletionItem(
				items, seenLabels, keyword, CompletionItemKind::Keyword, detail, keyword, "0_" + keyword, textEdit
			);
		}
	}
}

#include "completion_prefix.inl"

struct PartialMatches {
	bool any = false;
	bool exact = false;

	void include(const PartialMatches &other) {
		any = any || other.any;
		exact = exact || other.exact;
	}
};

PatternFrontier describePatternFrontier(
	const MatcherFrontier &matcherFrontier, SectionType sectionType, const SourceFile &sourceFile,
	std::optional<std::string_view> partial
) {
	PatternFrontier result;
	result.patternKind = sectionTypeToString(sectionType);
	result.canComplete = !partial && nodeHasVisibleDefinition(matcherFrontier.node, sourceFile);

	if (matcherFrontier.node) {
		for (const auto &[literal, child] : matcherFrontier.node->literalChildren) {
			if (partial && !literal.starts_with(*partial))
				continue;
			std::string remaining = literal.substr(partial ? partial->size() : 0);
			if (!remaining.empty() && subtreeHasVisibleDefinition(child, sourceFile))
				result.transitions.push_back({"literal", std::move(remaining)});
		}
		if (!partial && nodeAcceptsArgument(matcherFrontier.node, sourceFile))
			result.transitions.push_back({"argument", {}});
	}

	std::sort(
		result.transitions.begin(), result.transitions.end(),
		[](const PatternFrontierTransition &left, const PatternFrontierTransition &right) {
		return std::tie(left.kind, left.text) < std::tie(right.kind, right.text);
	}
	);
	return result;
}

std::string patternFrontierKey(const PatternFrontier &frontier) {
	std::string key = frontier.patternKind;
	key += frontier.canComplete ? "\1" : "\0";
	for (const PatternFrontierTransition &transition : frontier.transitions) {
		key += transition.kind;
		key += '\0';
		key += transition.text;
		key += '\1';
	}
	return key;
}

void appendPatternFrontiers(
	const CompletionContext &context, SectionType sectionType, const CompletionPrefix &prefix,
	std::vector<PatternFrontier> &frontiers, std::set<std::string> &seen
) {
	CompletionPrefix effectivePrefix = prefix;
	expandMultiWordVariableCompletionPrefix(context, sectionType, visibleVariableNames(context), effectivePrefix);
	SourceFile *sourceFile = completionSourceFile(context);
	auto append = [&](const std::vector<MatcherFrontierCandidate> &candidates, std::optional<std::string_view> partial) {
		for (const MatcherFrontierCandidate &candidate : candidates) {
			PatternFrontier frontier = describePatternFrontier(candidate.frontier, sectionType, *sourceFile, partial);
			if (!frontier.canComplete && frontier.transitions.empty())
				continue;
			if (seen.insert(patternFrontierKey(frontier)).second)
				frontiers.push_back(std::move(frontier));
		}
	};

	append(
		collectMatcherFrontierCandidates(context, sectionType, effectivePrefix.committed),
		effectivePrefix.partial ? std::optional<std::string_view>(effectivePrefix.partial->text) : std::nullopt
	);
	if (effectivePrefix.partial)
		append(collectMatcherFrontierCandidates(context, sectionType, effectivePrefix.normalized), std::nullopt);
}

PartialMatches collectPartialLiteralSuggestions(
	PatternTreeNode *node, std::string_view partial, std::vector<CompletionItem> &items, std::set<std::string> &seen,
	std::string_view detailPrefix, std::string_view sortPrefix, const SourceFile &sourceFile, const CompletionContext &context
) {
	PartialMatches matches;
	if (!node)
		return matches;

	std::vector<std::string> literals;
	for (const auto &[literal, child] : node->literalChildren) {
		if (!literal.starts_with(partial) || !subtreeHasVisibleDefinition(child, sourceFile))
			continue;
		matches.any = true;
		std::string suggestion = extendLiteralSuggestion(literal, child);
		if (literal.size() == partial.size()) {
			matches.exact = true;
			if (suggestion == literal)
				continue;
		}
		literals.push_back(std::move(suggestion));
	}

	std::sort(literals.begin(), literals.end());
	for (const std::string &literal : literals) {
		TextEdit textEdit;
		textEdit.range = makeRange(context.line, context.character - static_cast<int>(partial.size()), context.character);
		textEdit.newText = literal;
		addCompletionItem(
			items, seen, literal, CompletionItemKind::Keyword, std::string(detailPrefix), literal,
			std::string(sortPrefix) + literal, std::move(textEdit)
		);
	}
	return matches;
}

PartialMatches collectPartialVariableSuggestions(
	PatternTreeNode *node, const std::set<std::string> &variableNames, std::string_view partial,
	std::vector<CompletionItem> &items, std::set<std::string> &seen, const SourceFile &sourceFile,
	const CompletionContext &context
) {
	PartialMatches matches;
	if (!nodeAcceptsArgument(node, sourceFile))
		return matches;
	for (const std::string &name : variableNames) {
		if (!name.starts_with(partial))
			continue;
		matches.any = true;
		matches.exact = matches.exact || name.size() == partial.size();
	}
	collectVariableSuggestions(
		node, variableNames, items, seen, sourceFile, context, partial, completionSourceSuffixLength(context, partial)
	);
	return matches;
}

void collectStableFrontierSuggestions(
	const MatcherFrontier &frontier, std::string_view detailPrefix, const CompletionContext &context,
	std::vector<CompletionItem> &items, std::set<std::string> &seenLabels
) {
	SourceFile *sourceFile = completionSourceFile(context);
	TextEdit insertionEdit;
	insertionEdit.range = makeRange(context.line, context.character, context.character);

	collectNextLiteralSuggestions(
		frontier.node, items, seenLabels, std::string(detailPrefix) + " next token", "0_", *sourceFile, insertionEdit
	);
	if (!nodeAcceptsArgument(frontier.node, *sourceFile))
		return;

	PatternTreeNode *functionRoot = context.parseContext->patternTrees[(int)SectionType::Function];
	requireCompilerInvariant(functionRoot != nullptr, "completion substitution requires a function pattern tree");
	collectNextLiteralSuggestions(
		functionRoot, items, seenLabels, "function pattern substitution", "1_", *sourceFile, insertionEdit
	);

	std::set<std::string> variableNames = visibleVariableNames(context);
	variableNames.insert(frontier.acceptedVariables.begin(), frontier.acceptedVariables.end());
	collectVariableSuggestions(frontier.node, variableNames, items, seenLabels, *sourceFile, context);
}

void collectMatcherFrontierCompletions(
	SectionType sectionType, std::string_view detailPrefix, const CompletionContext &context, const std::string &linePrefix,
	std::vector<CompletionItem> &items, std::set<std::string> &seenLabels
) {
	CompletionPrefix prefix = splitCompletionPrefix(linePrefix);
	std::set<std::string> variableNames = visibleVariableNames(context);
	expandMultiWordVariableCompletionPrefix(context, sectionType, variableNames, prefix);
	std::optional<MatcherFrontier> committedFrontier = collectMatcherFrontier(context, sectionType, prefix.committed);
	if (!committedFrontier)
		return;

	if (!prefix.partial) {
		collectStableFrontierSuggestions(*committedFrontier, detailPrefix, context, items, seenLabels);
		return;
	}

	SourceFile *sourceFile = completionSourceFile(context);
	PartialMatches matches = collectPartialLiteralSuggestions(
		committedFrontier->node, prefix.partial->text, items, seenLabels, std::string(detailPrefix) + " token", "0_",
		*sourceFile, context
	);
	variableNames.insert(committedFrontier->acceptedVariables.begin(), committedFrontier->acceptedVariables.end());
	if (nodeAcceptsArgument(committedFrontier->node, *sourceFile)) {
		PatternTreeNode *functionRoot = context.parseContext->patternTrees[(int)SectionType::Function];
		requireCompilerInvariant(functionRoot != nullptr, "completion substitution requires a function pattern tree");
		matches.include(collectPartialLiteralSuggestions(
			functionRoot, prefix.partial->text, items, seenLabels, "function pattern substitution", "1_", *sourceFile, context
		));
		matches.include(collectPartialVariableSuggestions(
			committedFrontier->node, variableNames, prefix.partial->text, items, seenLabels, *sourceFile, context
		));
	}

	if (!matches.any || matches.exact) {
		std::optional<MatcherFrontier> fullFrontier = collectMatcherFrontier(context, sectionType, prefix.normalized);
		if (fullFrontier)
			collectStableFrontierSuggestions(*fullFrontier, detailPrefix, context, items, seenLabels);
	}
}

std::string getLinePrefixFromFile(const std::string &path, int zeroBasedLine, int zeroBasedCharacter) {
	std::ifstream file(path);
	if (!file) {
		return {};
	}

	std::string line;
	for (int i = 0; i <= zeroBasedLine; ++i) {
		if (!std::getline(file, line)) {
			return {};
		}
	}

	size_t character = std::clamp<int>(zeroBasedCharacter, 0, static_cast<int>(line.size()));
	return line.substr(0, character);
}

} // namespace

CompletionList collectCompletions(const CompletionContext &context) {
	const SyntaxConfig &syntax =
		context.parseContext ? syntaxConfigForSourcePath(*context.parseContext, context.uri) : SyntaxConfig{};
	if (startsWithImportKeyword(context.linePrefix, syntax.importKeyword)) {
		return collectImportPathCompletions(context);
	}

	CompletionList result;
	std::vector<CompletionItem> items;
	std::set<std::string> seenLabels;

	addKeywordCompletions(items, seenLabels, context);

	if (!hasCompilationStage(context.parseContext, ParseContext::CompilationStage::ResolvedPatterns)) {
		result.items = std::move(items);
		return result;
	}

	std::string matchPrefix = normalizeCompletionWhitespace(trimLeft(context.linePrefix));
	collectMatcherFrontierCompletions(SectionType::Function, "function pattern", context, matchPrefix, items, seenLabels);
	collectMatcherFrontierCompletions(SectionType::Section, "section pattern", context, matchPrefix, items, seenLabels);

	result.items = std::move(items);
	return result;
}

PatternFrontierList collectPatternFrontiers(const CompletionContext &context) {
	PatternFrontierList result;
	if (!hasCompilationStage(context.parseContext, ParseContext::CompilationStage::ResolvedPatterns))
		return result;

	std::string matchPrefix = normalizeCompletionWhitespace(trimLeft(context.linePrefix));
	CompletionPrefix prefix = splitCompletionPrefix(matchPrefix);
	std::set<std::string> seen;
	appendPatternFrontiers(context, SectionType::Function, prefix, result.frontiers, seen);
	appendPatternFrontiers(context, SectionType::Section, prefix, result.frontiers, seen);
	return result;
}

bool patternLineCanTerminate(const CompletionContext &context) {
	if (normalizeCompletionWhitespace(trimLeft(context.linePrefix)).empty())
		return true;
	PatternFrontierList frontiers = collectPatternFrontiers(context);
	return std::any_of(frontiers.frontiers.begin(), frontiers.frontiers.end(), [](const PatternFrontier &frontier) {
		return frontier.canComplete;
	});
}

bool canContinuePatternSource(const CompletionContext &context, std::string_view continuation) {
	CompletionContext candidateContext = context;
	size_t segmentStart = 0;
	while (true) {
		size_t newline = continuation.find('\n', segmentStart);
		candidateContext.linePrefix.append(continuation.substr(segmentStart, newline - segmentStart));
		candidateContext.character = static_cast<int>(candidateContext.linePrefix.size());
		if (newline == std::string_view::npos)
			return !collectPatternFrontiers(candidateContext).frontiers.empty();
		if (!patternLineCanTerminate(candidateContext))
			return false;
		candidateContext.linePrefix.clear();
		candidateContext.line++;
		candidateContext.character = 0;
		segmentStart = newline + 1;
	}
}

FilterContinuationsResult
filterPatternContinuations(const CompletionContext &context, const std::vector<std::string> &continuations) {
	FilterContinuationsResult result;
	if (!hasCompilationStage(context.parseContext, ParseContext::CompilationStage::ResolvedPatterns))
		return result;
	for (size_t index = 0; index < continuations.size(); index++) {
		if (canContinuePatternSource(context, continuations[index]))
			result.accepted.push_back(index);
	}
	return result;
}

std::string
renderCompletionDebugReport(ParseContext &context, const std::string &path, int zeroBasedLine, int zeroBasedCharacter) {
	std::string sourcePrefix = getLinePrefixFromFile(path, zeroBasedLine, zeroBasedCharacter);
	CompletionContext completionContext = makeCompletionContext(
		&context, pathutil::toAbsoluteUri(path), sourcePrefix, std::filesystem::current_path().string(), zeroBasedLine,
		zeroBasedCharacter
	);

	CompletionList completions = collectCompletions(completionContext);

	std::ostringstream out;
	out << "completion debug\n";
	out << "path: " << path << "\n";
	out << "position: " << (zeroBasedLine + 1) << ":" << (zeroBasedCharacter + 1) << "\n";
	out << "line prefix: " << completionContext.linePrefix << "\n";
	out << "match prefix: " << normalizeCompletionWhitespace(trimLeft(completionContext.linePrefix)) << "\n";
	out << "normalized prefix: "
		<< normalizeCompletionPatternPrefix(normalizeCompletionWhitespace(trimLeft(completionContext.linePrefix))) << "\n";
	out << "elements:";

	auto elements = getPatternElements(
		normalizeCompletionPatternPrefix(normalizeCompletionWhitespace(trimLeft(completionContext.linePrefix)))
	);
	if (elements.empty()) {
		out << " <none>\n";
	} else {
		out << "\n";
		for (const PatternElement &element : elements) {
			out << "  - type=" << static_cast<int>(element.type) << " text=\"" << element.text << "\"\n";
		}
	}

	out << "suggestions: " << completions.items.size() << "\n";
	for (const CompletionItem &item : completions.items) {
		out << "  - label=\"" << item.label << "\"";
		if (item.detail) {
			out << " detail=\"" << *item.detail << "\"";
		}
		if (item.insertText) {
			out << " insert=\"" << *item.insertText << "\"";
		}
		if (item.sortText) {
			out << " sort=\"" << *item.sortText << "\"";
		}
		if (item.textEdit) {
			out << " edit=(" << item.textEdit->range.start.line + 1 << ":" << item.textEdit->range.start.character + 1 << "-"
				<< item.textEdit->range.end.line + 1 << ":" << item.textEdit->range.end.character + 1 << " -> "
				<< item.textEdit->newText << ")";
		}
		out << "\n";
	}

	return out.str();
}

} // namespace lsp
