#include "completion.h"
#include "expression.h"
#include "parseContext.h"
#include "pathUtils.h"
#include "pattern/patternReference.h"
#include "pattern/pattern_tree/matchProgress.h"
#include "pattern/pattern_tree/patternElement.h"
#include "patternTreeNode.h"
#include "syntaxConfig.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

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

std::vector<PatternTreeNode *> deduplicateNodes(const std::vector<PatternTreeNode *> &nodes) {
	std::vector<PatternTreeNode *> result;
	std::set<PatternTreeNode *> seen;
	for (PatternTreeNode *node : nodes) {
		if (node && seen.insert(node).second) {
			result.push_back(node);
		}
	}
	return result;
}

std::vector<PatternTreeNode *>
advanceCompletionStates(const std::vector<PatternTreeNode *> &states, const PatternElement &element) {
	std::vector<PatternTreeNode *> next;

	for (PatternTreeNode *node : states) {
		if (!node)
			continue;

		if (element.type == PatternElement::Type::VariableLike) {
			auto literalIt = node->literalChildren.find(element.text);
			if (literalIt != node->literalChildren.end()) {
				next.push_back(literalIt->second);
			}
			if (node->argumentChild) {
				next.push_back(node->argumentChild);
			}
			if (node->wordChild) {
				next.push_back(node->wordChild);
			}
		} else if (element.type == PatternElement::Type::Variable) {
			if (node->argumentChild) {
				next.push_back(node->argumentChild);
			}
		} else {
			auto literalIt = node->literalChildren.find(element.text);
			if (literalIt != node->literalChildren.end()) {
				next.push_back(literalIt->second);
			}
		}
	}

	return deduplicateNodes(next);
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
	while (current && current->literalChildren.size() == 1 && !current->argumentChild && !current->wordChild &&
		   current->matchingDefinitions.empty() && literal.size() < 64) {
		const auto &[nextLiteral, nextNode] = *current->literalChildren.begin();
		literal += nextLiteral;
		current = nextNode;
	}
	return literal;
}

void collectNextLiteralSuggestions(
	const std::vector<PatternTreeNode *> &states, std::vector<CompletionItem> &items, std::set<std::string> &seen,
	std::string_view detailPrefix
) {
	std::vector<std::string> literals;
	for (PatternTreeNode *node : states) {
		if (!node)
			continue;
		for (const auto &[text, child] : node->literalChildren) {
			if (text == " " && child && !child->literalChildren.empty()) {
				for (const auto &[nextText, nextChild] : child->literalChildren) {
					literals.push_back(extendLiteralSuggestion(text + nextText, nextChild));
				}
			}
			literals.push_back(extendLiteralSuggestion(text, child));
		}
	}
	std::sort(literals.begin(), literals.end());
	for (const std::string &literal : literals) {
		addCompletionItem(
			items, seen, literal, CompletionItemKind::Keyword, std::string(detailPrefix), literal, "1_" + literal
		);
	}
}

std::string firstParameterName(const PatternTreeNode *node) {
	if (!node) {
		return {};
	}
	for (const auto &[_, name] : node->parameterNames) {
		if (!name.empty()) {
			return name;
		}
	}
	return {};
}

void collectPlaceholderSuggestions(
	const std::vector<PatternTreeNode *> &states, std::vector<CompletionItem> &items, std::set<std::string> &seen,
	std::string_view detailPrefix
) {
	auto addPlaceholderSuggestionsForNode = [&](PatternTreeNode *node) {
		if (!node)
			return;
		if (node->argumentChild) {
			std::string name = firstParameterName(node->argumentChild);
			if (name.empty()) {
				name = "expression";
			}
			addCompletionItem(
				items, seen, "<" + name + ">", CompletionItemKind::Snippet, std::string(detailPrefix) + " argument", {},
				"2_" + name
			);
		}
		if (node->wordChild) {
			std::string name = firstParameterName(node->wordChild);
			if (name.empty()) {
				name = "word";
			}
			addCompletionItem(
				items, seen, "<" + name + ">", CompletionItemKind::Snippet, std::string(detailPrefix) + " captured word", {},
				"2_" + name
			);
		}
	};

	for (PatternTreeNode *node : states) {
		if (!node)
			continue;
		addPlaceholderSuggestionsForNode(node);
		auto spaceIt = node->literalChildren.find(" ");
		if (spaceIt != node->literalChildren.end()) {
			addPlaceholderSuggestionsForNode(spaceIt->second);
		}
	}
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

void collectPatternTreeCompletions(
	PatternTreeNode *root, std::string_view detailPrefix, const CompletionContext &context, const std::string &linePrefix,
	std::vector<CompletionItem> &items, std::set<std::string> &seenLabels
) {
	if (!root) {
		return;
	}

	std::string normalizedPrefix = normalizeCompletionPatternPrefix(linePrefix);
	auto elements = getPatternElements(normalizedPrefix);

	bool hasPartialElement = false;
	PatternElement partialElement(PatternElement::Type::Other);
	if (!normalizedPrefix.empty() && !std::isspace(static_cast<unsigned char>(normalizedPrefix.back())) && !elements.empty()) {
		PatternElement last = elements.back();
		if (last.type == PatternElement::Type::VariableLike || last.type == PatternElement::Type::Other) {
			hasPartialElement = true;
			partialElement = last;
			elements.pop_back();
		}
	}

	std::vector<PatternTreeNode *> states = {root};
	for (const PatternElement &element : elements) {
		states = advanceCompletionStates(states, element);
		if (states.empty()) {
			return;
		}
	}

	if (hasPartialElement) {
		std::vector<PatternTreeNode *> exactMatchStates;
		std::vector<std::pair<std::string, std::string>> partialSuggestions;

		for (PatternTreeNode *node : states) {
			for (const auto &[literal, child] : node->literalChildren) {
				if (!literal.starts_with(partialElement.text)) {
					continue;
				}
				if (literal.size() == partialElement.text.size()) {
					exactMatchStates.push_back(child);
				} else {
					partialSuggestions.push_back({literal, literal.substr(partialElement.text.size())});
				}
			}
		}

		std::sort(partialSuggestions.begin(), partialSuggestions.end());
		for (const auto &[label, suffix] : partialSuggestions) {
			TextEdit textEdit;
			textEdit.range =
				makeRange(context.line, context.character - static_cast<int>(partialElement.text.size()), context.character);
			textEdit.newText = label;
			addCompletionItem(
				items, seenLabels, label, CompletionItemKind::Keyword, std::string(detailPrefix) + " token", label,
				"1_" + label, textEdit
			);
		}

		exactMatchStates = deduplicateNodes(exactMatchStates);
		collectNextLiteralSuggestions(exactMatchStates, items, seenLabels, std::string(detailPrefix) + " next token");
		collectPlaceholderSuggestions(exactMatchStates, items, seenLabels, detailPrefix);
		return;
	}

	collectNextLiteralSuggestions(states, items, seenLabels, std::string(detailPrefix) + " next token");
	collectPlaceholderSuggestions(states, items, seenLabels, detailPrefix);
}

std::vector<MatchProgress>
collectMatcherFrontier(const CompletionContext &context, SectionType sectionType, const std::string &linePrefix) {
	std::vector<MatchProgress> frontier;
	if (!context.parseContext || !context.parseContext->patternTrees[(int)sectionType]) {
		return frontier;
	}

	std::string normalizedPrefix = normalizeCompletionPatternPrefix(linePrefix);
	Expression expr;
	expr.range.subString = normalizedPrefix;
	PatternReference reference(&expr, sectionType);
	reference.patternElements = getPatternElements(reference.pattern.text);

	std::vector<MatchProgress> queue = {MatchProgress(context.parseContext, &reference)};
	size_t iterations = 0;
	while (!queue.empty() && iterations++ < 2048) {
		MatchProgress current = queue.back();
		queue.pop_back();
		if (current.sourceElementIndex == reference.patternElements.size()) {
			frontier.push_back(current);
		}
		std::vector<MatchProgress> nextSteps = current.step();
		queue.insert(queue.end(), std::make_move_iterator(nextSteps.begin()), std::make_move_iterator(nextSteps.end()));
	}

	return frontier;
}

void collectMatcherFrontierCompletions(
	SectionType sectionType, std::string_view detailPrefix, const CompletionContext &context, const std::string &linePrefix,
	std::vector<CompletionItem> &items, std::set<std::string> &seenLabels
) {
	std::vector<MatchProgress> frontier = collectMatcherFrontier(context, sectionType, linePrefix);
	if (frontier.empty()) {
		return;
	}

	std::vector<PatternTreeNode *> states;
	for (const MatchProgress &progress : frontier) {
		states.push_back(progress.currentNode);
	}
	states = deduplicateNodes(states);

	TextEdit insertionEdit;
	insertionEdit.range = makeRange(context.line, context.character, context.character);

	std::vector<CompletionItem> frontierItems;
	std::set<std::string> localSeen;
	collectNextLiteralSuggestions(states, frontierItems, localSeen, std::string(detailPrefix) + " next token");
	collectPlaceholderSuggestions(states, frontierItems, localSeen, detailPrefix);

	for (CompletionItem &item : frontierItems) {
		item.textEdit = insertionEdit;
		if (!item.insertText) {
			item.insertText = item.label;
		}
		item.textEdit->newText = *item.insertText;
		if (seenLabels.insert(item.label).second) {
			items.push_back(std::move(item));
		}
	}
}

void collectExpressionPostfixCompletions(
	const CompletionContext &context, const std::string &linePrefix, std::vector<CompletionItem> &items,
	std::set<std::string> &seenLabels
) {
	if (!context.parseContext || linePrefix.empty() || !std::isspace(static_cast<unsigned char>(linePrefix.back()))) {
		return;
	}

	PatternTreeNode *root = context.parseContext->patternTrees[(int)SectionType::Function];
	if (!root || !root->argumentChild) {
		return;
	}

	std::vector<PatternTreeNode *> states = {root->argumentChild};
	std::vector<CompletionItem> postfixItems;
	std::set<std::string> localSeen;
	collectNextLiteralSuggestions(states, postfixItems, localSeen, "function pattern next token");
	collectPlaceholderSuggestions(states, postfixItems, localSeen, "function pattern");

	TextEdit insertionEdit;
	insertionEdit.range = makeRange(context.line, context.character, context.character);

	for (CompletionItem &item : postfixItems) {
		item.textEdit = insertionEdit;
		if (!item.insertText) {
			item.insertText = item.label;
		}
		if (!item.insertText->empty() && item.insertText->front() == ' ') {
			item.insertText = item.insertText->substr(1);
		}
		item.textEdit->newText = *item.insertText;
		if (seenLabels.insert(item.label).second) {
			items.push_back(std::move(item));
		}
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
	collectPatternTreeCompletions(
		context.parseContext->patternTrees[(int)SectionType::Function], "function pattern", context, matchPrefix, items,
		seenLabels
	);
	collectPatternTreeCompletions(
		context.parseContext->patternTrees[(int)SectionType::Section], "section pattern", context, matchPrefix, items,
		seenLabels
	);
	collectMatcherFrontierCompletions(SectionType::Function, "function pattern", context, matchPrefix, items, seenLabels);
	collectMatcherFrontierCompletions(SectionType::Section, "section pattern", context, matchPrefix, items, seenLabels);
	collectExpressionPostfixCompletions(context, matchPrefix, items, seenLabels);

	result.items = std::move(items);
	return result;
}

std::string
renderCompletionDebugReport(ParseContext &context, const std::string &path, int zeroBasedLine, int zeroBasedCharacter) {
	CompletionContext completionContext{
		.parseContext = &context,
		.uri = pathutil::toAbsoluteUri(path),
		.linePrefix = getLinePrefixFromFile(path, zeroBasedLine, zeroBasedCharacter),
		.workspaceRootPath = std::filesystem::current_path().string(),
		.line = zeroBasedLine,
		.character = zeroBasedCharacter,
	};

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
