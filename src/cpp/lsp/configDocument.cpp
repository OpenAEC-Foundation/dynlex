#include "configDocument.h"
#include "completion.h"
#include "semanticTokenBuilder.h"
#include "semanticTokenDebug.h"
#include "syntaxConfig.h"
#include "textDocument.h"
#include <algorithm>
#include <cctype>
#include <memory>
#include <set>
#include <unordered_set>
#include <vector>

using namespace std::literals;

namespace lsp {

namespace {

struct ConfigEntry {
	int line = 0;
	int indentLevel = 0;
	int keyStart = 0;
	int keyEnd = 0;
	int lineEnd = 0;
	int valueStart = -1;
	int valueEnd = -1;
	bool valueQuoted = false;
	std::string key;
	std::string value;
};

struct ConfigNode {
	ConfigEntry entry;
	ConfigNode *parent{};
	std::vector<std::unique_ptr<ConfigNode>> children;
};

Position makePosition(int line, int character) { return {.line = line, .character = character}; }

Range makeRange(int line, int start, int end) { return {.start = makePosition(line, start), .end = makePosition(line, end)}; }

void addDiagnostic(
	std::vector<Diagnostic> &diagnostics, int line, int start, int end, std::string message,
	DiagnosticSeverity severity = DiagnosticSeverity::Error
) {
	Diagnostic diag;
	diag.range = makeRange(line, start, std::max(start, end));
	diag.message = std::move(message);
	diag.severity = severity;
	diag.source = "dynlex";
	diagnostics.push_back(std::move(diag));
}

TextEdit makeTextEdit(int line, int start, int end, std::string newText) {
	TextEdit edit;
	edit.range = makeRange(line, start, end);
	edit.newText = std::move(newText);
	return edit;
}

std::string_view trimView(std::string_view text) {
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
		text.remove_prefix(1);
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
		text.remove_suffix(1);
	return text;
}

bool hasWhitespace(std::string_view text) {
	return std::any_of(text.begin(), text.end(), [](unsigned char c) {
		return std::isspace(c);
	});
}

void addConfigLineTokens(SemanticTokenBuilder &builder, int lineIndex, std::string_view line) {
	size_t commentPos = findCommentStart(line, "#");
	std::string_view code = commentPos == std::string_view::npos ? line : line.substr(0, commentPos);
	size_t i = 0;
	while (i < code.size()) {
		if (std::isspace(static_cast<unsigned char>(code[i]))) {
			i++;
			continue;
		}
		if (code[i] == '"') {
			size_t start = i++;
			bool escaped = false;
			while (i < code.size()) {
				char c = code[i++];
				if (!escaped && c == '"')
					break;
				escaped = (!escaped && c == '\\');
				if (c != '\\')
					escaped = false;
			}
			builder.add(
				lineIndex, {static_cast<int>(start), static_cast<int>(std::min(i, code.size())), SemanticTokenType::String, 0}
			);
			continue;
		}
		size_t start = i;
		while (i < code.size() && !std::isspace(static_cast<unsigned char>(code[i])) && code[i] != '"')
			i++;
		builder.add(lineIndex, {static_cast<int>(start), static_cast<int>(i), SemanticTokenType::Keyword, 0});
	}
	if (commentPos != std::string_view::npos) {
		builder.add(lineIndex, {static_cast<int>(commentPos), static_cast<int>(line.size()), SemanticTokenType::Comment, 0});
	}
}

bool parseConfigDocument(
	const TextDocument &document, std::vector<Diagnostic> &diagnostics, std::vector<std::unique_ptr<ConfigNode>> &roots
) {
	std::vector<ConfigNode *> stack;
	std::string indentUnit;
	int previousIndentLevel = 0;
	ConfigNode *previousNode = nullptr;

	for (int lineIndex = 0; lineIndex < document.lineCount(); ++lineIndex) {
		std::string_view originalLine = document.getLine(lineIndex);
		size_t commentPos = findCommentStart(originalLine, "#");
		std::string_view code = commentPos == std::string_view::npos ? originalLine : originalLine.substr(0, commentPos);
		size_t lineLen = code.size();
		while (lineLen > 0 && std::isspace(static_cast<unsigned char>(code[lineLen - 1])))
			lineLen--;
		code = code.substr(0, lineLen);
		if (trimView(code).empty())
			continue;

		size_t indentLength = 0;
		while (indentLength < code.size() && std::isspace(static_cast<unsigned char>(code[indentLength])))
			indentLength++;
		std::string_view indent = code.substr(0, indentLength);
		if (!indent.empty()) {
			char indentChar = indent.front();
			size_t invalidCharIndex = indent.find_first_not_of(indentChar);
			if (invalidCharIndex != std::string_view::npos) {
				addDiagnostic(
					diagnostics, lineIndex, 0, static_cast<int>(indent.size()), "mixed indentation is not allowed in config.dl"
				);
				return false;
			}
			if (indentUnit.empty())
				indentUnit = std::string(indent);
			if (indent.size() % indentUnit.size() != 0) {
				addDiagnostic(
					diagnostics, lineIndex, 0, static_cast<int>(indent.size()),
					"indentation does not match the earlier config.dl indentation"
				);
				return false;
			}
		}

		int indentLevel = indent.empty() ? 0 : static_cast<int>(indent.size() / indentUnit.size());
		if (indentLevel > previousIndentLevel + 1) {
			addDiagnostic(
				diagnostics, lineIndex, 0, static_cast<int>(indent.size()),
				"indentation can only increase by one level at a time"
			);
			return false;
		}
		if (indentLevel > previousIndentLevel && !previousNode) {
			addDiagnostic(
				diagnostics, lineIndex, 0, static_cast<int>(indent.size()), "indented config entry must follow a parent entry"
			);
			return false;
		}
		if (indentLevel > previousIndentLevel) {
			stack.push_back(previousNode);
		} else if (indentLevel < previousIndentLevel) {
			stack.resize(static_cast<size_t>(indentLevel));
		}
		previousIndentLevel = indentLevel;

		std::string_view trimmed = code.substr(indentLength);
		size_t colonPos = trimmed.find(':');
		if (colonPos == std::string_view::npos) {
			addDiagnostic(
				diagnostics, lineIndex, static_cast<int>(indentLength), static_cast<int>(trimmed.size()),
				"expected 'key: value' or 'key:'"
			);
			return false;
		}

		std::string_view keyView = trimView(trimmed.substr(0, colonPos));
		if (keyView.empty()) {
			addDiagnostic(
				diagnostics, lineIndex, static_cast<int>(indentLength), static_cast<int>(indentLength + colonPos),
				"config key cannot be empty"
			);
			return false;
		}

		ConfigEntry entry;
		entry.line = lineIndex;
		entry.indentLevel = indentLevel;
		entry.keyStart = static_cast<int>(indentLength + trimmed.substr(0, colonPos).find_first_not_of(" \t"));
		entry.keyEnd = entry.keyStart + static_cast<int>(keyView.size());
		entry.lineEnd = static_cast<int>(code.size());
		entry.key = std::string(keyView);

		std::string_view valueView = trimView(trimmed.substr(colonPos + 1));
		if (!valueView.empty()) {
			entry.valueStart = static_cast<int>(trimmed.find(valueView) + indentLength);
			entry.valueEnd = entry.valueStart + static_cast<int>(valueView.size());
			if (valueView.front() == '"') {
				entry.valueQuoted = true;
				if (valueView.size() < 2 || valueView.back() != '"') {
					addDiagnostic(diagnostics, lineIndex, entry.valueStart, entry.valueEnd, "unterminated string in config.dl");
					return false;
				}
				entry.value = std::string(valueView.substr(1, valueView.size() - 2));
			} else {
				entry.value = std::string(valueView);
			}
		}

		auto node = std::make_unique<ConfigNode>();
		node->entry = std::move(entry);
		ConfigNode *nodePtr = node.get();
		if (stack.empty()) {
			roots.push_back(std::move(node));
		} else {
			node->parent = stack.back();
			stack.back()->children.push_back(std::move(node));
		}
		previousNode = nodePtr;
	}

	if (roots.empty()) {
		addDiagnostic(diagnostics, 0, 0, 0, "config.dl is empty");
		return false;
	}
	return true;
}

std::string nodePath(const ConfigNode &node) {
	std::vector<std::string> parts;
	for (const ConfigNode *current = &node; current; current = current->parent)
		parts.push_back(current->entry.key);
	std::reverse(parts.begin(), parts.end());
	std::string result;
	for (size_t i = 0; i < parts.size(); ++i) {
		if (i)
			result += ".";
		result += parts[i];
	}
	return result;
}

const ConfigNode *findChild(const ConfigNode &node, std::string_view key) {
	for (const auto &child : node.children) {
		if (child->entry.key == key)
			return child.get();
	}
	return nullptr;
}

bool validateNameNode(
	const ConfigNode &node, std::vector<Diagnostic> &diagnostics, bool allowChildren = false, bool requireValue = true,
	bool allowWhitespace = false
) {
	const ConfigNode *nameChild = findChild(node, "name");
	if (!node.entry.value.empty() && nameChild) {
		addDiagnostic(
			diagnostics, node.entry.line, node.entry.keyStart, node.entry.keyEnd,
			"config entry '" + nodePath(node) + "' cannot define both a direct value and a nested name"
		);
		return false;
	}

	std::string_view value = !node.entry.value.empty()
								 ? std::string_view(node.entry.value)
								 : (nameChild ? std::string_view(nameChild->entry.value) : std::string_view{});
	if (value.empty()) {
		if (!requireValue)
			return true;
		addDiagnostic(
			diagnostics, node.entry.line, node.entry.keyStart, node.entry.keyEnd,
			"config entry '" + nodePath(node) + "' is missing a name"
		);
		return false;
	}
	if (!allowWhitespace && hasWhitespace(value)) {
		int start = !node.entry.value.empty() ? node.entry.valueStart : nameChild->entry.valueStart;
		int end = !node.entry.value.empty() ? node.entry.valueEnd : nameChild->entry.valueEnd;
		addDiagnostic(
			diagnostics, !node.entry.value.empty() ? node.entry.line : nameChild->entry.line, start, end,
			"config entry '" + nodePath(node) + "' must be a single token"
		);
		return false;
	}
	if (allowWhitespace) {
		bool invalidSpacing = value.front() == ' ' || value.back() == ' ' || value.contains("  ") ||
							  std::any_of(value.begin(), value.end(), [](unsigned char character) {
			return std::isspace(character) && character != ' ';
		});
		if (invalidSpacing) {
			int start = !node.entry.value.empty() ? node.entry.valueStart : nameChild->entry.valueStart;
			int end = !node.entry.value.empty() ? node.entry.valueEnd : nameChild->entry.valueEnd;
			addDiagnostic(
				diagnostics, !node.entry.value.empty() ? node.entry.line : nameChild->entry.line, start, end,
				"config entry '" + nodePath(node) + "' must contain tokens separated by single spaces"
			);
			return false;
		}
	}
	if (!allowChildren) {
		for (const auto &child : node.children) {
			if (child.get() != nameChild) {
				addDiagnostic(
					diagnostics, child->entry.line, child->entry.keyStart, child->entry.keyEnd,
					"unknown nested config key '" + child->entry.key + "' under '" + nodePath(node) + "'"
				);
				return false;
			}
		}
	}
	return true;
}

bool validateConfigTree(const std::vector<std::unique_ptr<ConfigNode>> &roots, std::vector<Diagnostic> &diagnostics) {
	if (roots.size() != 1 || roots[0]->entry.key != "dynlex options") {
		const ConfigNode &root = *roots[0];
		addDiagnostic(
			diagnostics, root.entry.line, root.entry.keyStart, root.entry.keyEnd, "config.dl must start with 'dynlex options:'"
		);
		return false;
	}

	SyntaxConfig syntax;
	const ConfigNode *shorthandNode = nullptr;
	for (const auto &child : roots[0]->children) {
		const ConfigNode &node = *child;
		if (node.entry.key == "import" || node.entry.key == "comment" || node.entry.key == "open section" ||
			node.entry.key == "section" || node.entry.key == "function" || node.entry.key == "flex" ||
			node.entry.key == "local") {
			if (!validateNameNode(node, diagnostics))
				return false;
			const ConfigNode *nameChild = findChild(node, "name");
			std::string_view configuredName =
				!node.entry.value.empty() ? std::string_view(node.entry.value) : std::string_view(nameChild->entry.value);
			if (node.entry.key == "section")
				syntax.sectionName = configuredName;
			else if (node.entry.key == "function")
				syntax.functionName = configuredName;
			else if (node.entry.key == "flex")
				syntax.flexName = configuredName;
			else if (node.entry.key == "local")
				syntax.localName = configuredName;
			continue;
		}
		if (node.entry.key == "class") {
			if (!validateNameNode(node, diagnostics, true, false))
				return false;
			const ConfigNode *nameChild = findChild(node, "name");
			if (!node.entry.value.empty())
				syntax.className = node.entry.value;
			else if (nameChild)
				syntax.className = nameChild->entry.value;
			for (const auto &grandChild : node.children) {
				if (grandChild.get() == nameChild)
					continue;
				if (grandChild->entry.key == "members") {
					if (!validateNameNode(*grandChild, diagnostics))
						return false;
					const ConfigNode *nestedName = findChild(*grandChild, "name");
					syntax.membersSectionName =
						!grandChild->entry.value.empty() ? grandChild->entry.value : nestedName->entry.value;
					continue;
				}
				addDiagnostic(
					diagnostics, grandChild->entry.line, grandChild->entry.keyStart, grandChild->entry.keyEnd,
					"unknown config key '" + grandChild->entry.key + "' under 'class'"
				);
				return false;
			}
			continue;
		}
		if (node.entry.key == "shorthand definitions") {
			shorthandNode = &node;
			if (!node.entry.value.empty()) {
				addDiagnostic(
					diagnostics, node.entry.line, node.entry.valueStart, node.entry.valueEnd,
					"config entry 'shorthand definitions' cannot define a value"
				);
				return false;
			}
			for (const auto &grandChild : node.children) {
				std::string *target = nullptr;
				if (grandChild->entry.key == "action")
					target = &syntax.actionShorthand;
				else if (grandChild->entry.key == "value")
					target = &syntax.valueShorthand;
				else if (grandChild->entry.key == "replacement")
					target = &syntax.replacementShorthand;
				else {
					addDiagnostic(
						diagnostics, grandChild->entry.line, grandChild->entry.keyStart, grandChild->entry.keyEnd,
						"unknown config key '" + grandChild->entry.key + "' under 'shorthand definitions'"
					);
					return false;
				}
				if (!validateNameNode(*grandChild, diagnostics, false, true, true))
					return false;
				const ConfigNode *nameChild = findChild(*grandChild, "name");
				*target = !grandChild->entry.value.empty() ? grandChild->entry.value : nameChild->entry.value;
			}
			continue;
		}
		if (node.entry.key == "messages") {
			static const std::unordered_set<std::string> allowed = {
				"could not import main file",
				"failed to import source file",
				"invalid indentation amount",
				"invalid indentation character",
				"invalid indentation increase",
				"missing body section",
				"unknown section",
				"unexpected class line",
				"shorthand definition requires pattern",
				"replacement shorthand requires inline body",
				"value shorthand requires multiline body",
				"action shorthand returns value",
				"value shorthand returns nothing",
				"replacement shorthand returns nothing",
			};
			for (const auto &grandChild : node.children) {
				if (!allowed.contains(grandChild->entry.key)) {
					addDiagnostic(
						diagnostics, grandChild->entry.line, grandChild->entry.keyStart, grandChild->entry.keyEnd,
						"unknown config key '" + grandChild->entry.key + "' under 'messages'"
					);
					return false;
				}
				if (grandChild->entry.value.empty()) {
					addDiagnostic(
						diagnostics, grandChild->entry.line, grandChild->entry.keyStart, grandChild->entry.keyEnd,
						"config entry 'messages." + grandChild->entry.key + "' must define a value"
					);
					return false;
				}
			}
			continue;
		}
		if (node.entry.key == "child sections") {
			static const std::unordered_set<std::string> allowed = {"execute", "replacement", "patterns",
																	"globals", "before",	  "after"};
			for (const auto &grandChild : node.children) {
				if (!allowed.contains(grandChild->entry.key)) {
					addDiagnostic(
						diagnostics, grandChild->entry.line, grandChild->entry.keyStart, grandChild->entry.keyEnd,
						"unknown config key '" + grandChild->entry.key + "' under 'child sections'"
					);
					return false;
				}
				if (!validateNameNode(*grandChild, diagnostics))
					return false;
				const ConfigNode *nameChild = findChild(*grandChild, "name");
				std::string configuredName =
					!grandChild->entry.value.empty() ? grandChild->entry.value : nameChild->entry.value;
				if (grandChild->entry.key == "execute")
					syntax.executeSectionName = std::move(configuredName);
				else if (grandChild->entry.key == "replacement")
					syntax.replacementSectionName = std::move(configuredName);
				else if (grandChild->entry.key == "patterns")
					syntax.patternsSectionName = std::move(configuredName);
				else if (grandChild->entry.key == "globals")
					syntax.globalsSectionName = std::move(configuredName);
				else if (grandChild->entry.key == "before")
					syntax.beforeSectionName = std::move(configuredName);
				else if (grandChild->entry.key == "after")
					syntax.afterSectionName = std::move(configuredName);
			}
			continue;
		}
		addDiagnostic(
			diagnostics, node.entry.line, node.entry.keyStart, node.entry.keyEnd, "unknown config key '" + node.entry.key + "'"
		);
		return false;
	}

	if (std::optional<std::string> error = definitionShorthandSyntaxError(syntax)) {
		const ConfigNode &diagnosticNode = shorthandNode ? *shorthandNode : *roots[0];
		addDiagnostic(
			diagnostics, diagnosticNode.entry.line, diagnosticNode.entry.keyStart, diagnosticNode.entry.keyEnd, *error
		);
		return false;
	}
	return true;
}

} // namespace

bool isConfigDocumentUri(std::string_view uri) {
	size_t slash = uri.find_last_of("/\\");
	std::string_view name = slash == std::string_view::npos ? uri : uri.substr(slash + 1);
	return name == "config.dl";
}

std::vector<Diagnostic> collectConfigDiagnostics(const TextDocument &document) {
	std::vector<Diagnostic> diagnostics;
	std::vector<std::unique_ptr<ConfigNode>> roots;
	if (!parseConfigDocument(document, diagnostics, roots))
		return diagnostics;
	validateConfigTree(roots, diagnostics);
	return diagnostics;
}

std::vector<int> encodeConfigSemanticTokens(const TextDocument &document) {
	SemanticTokenBuilder builder(document.lineCount());
	for (int lineIndex = 0; lineIndex < document.lineCount(); ++lineIndex)
		addConfigLineTokens(builder, lineIndex, document.getLine(lineIndex));
	return encodeSemanticTokens(builder.tokenLines());
}

CompletionList collectConfigCompletions(const TextDocument &document, int line, int character) {
	CompletionList result;
	std::vector<CompletionItem> items;
	std::set<std::string> seen;

	auto addItem = [&](std::string label, std::string detail, std::string replacement) {
		if (!seen.insert(label).second)
			return;
		CompletionItem item;
		item.label = std::move(label);
		item.kind = CompletionItemKind::Keyword;
		item.detail = std::move(detail);
		item.insertText = replacement;
		item.sortText = "0_" + item.label;
		item.textEdit = makeTextEdit(line, 0, character, std::move(replacement));
		items.push_back(std::move(item));
	};

	std::string_view fullLine = document.getLine(line);
	std::string_view linePrefixView = fullLine.substr(0, std::min<int>(character, static_cast<int>(fullLine.size())));
	size_t commentPos = findCommentStart(linePrefixView, "#");
	if (commentPos != std::string_view::npos)
		linePrefixView = linePrefixView.substr(0, commentPos);

	size_t indentLength = 0;
	while (indentLength < linePrefixView.size() && std::isspace(static_cast<unsigned char>(linePrefixView[indentLength])))
		indentLength++;
	std::string indent(linePrefixView.substr(0, indentLength));
	std::string trimmed = std::string(trimView(linePrefixView.substr(indentLength)));

	struct StackEntry {
		int indent = 0;
		std::string key;
	};
	std::vector<StackEntry> stack;
	for (int i = 0; i < line; ++i) {
		std::string_view prevLine = document.getLine(i);
		size_t prevCommentPos = findCommentStart(prevLine, "#");
		if (prevCommentPos != std::string_view::npos)
			prevLine = prevLine.substr(0, prevCommentPos);
		prevLine = trimView(prevLine);
		if (prevLine.empty())
			continue;

		size_t rawIndent = 0;
		std::string_view fullLine = document.getLine(i);
		while (rawIndent < fullLine.size() && std::isspace(static_cast<unsigned char>(fullLine[rawIndent])))
			rawIndent++;
		size_t colon = prevLine.find(':');
		if (colon == std::string_view::npos)
			continue;
		std::string key = std::string(trimView(prevLine.substr(0, colon)));
		std::string_view value = trimView(prevLine.substr(colon + 1));
		while (!stack.empty() && stack.back().indent >= static_cast<int>(rawIndent))
			stack.pop_back();
		if (value.empty())
			stack.push_back({static_cast<int>(rawIndent), std::move(key)});
	}

	std::string parent = stack.empty() ? "" : stack.back().key;
	auto startsLike = [&](std::string_view candidate) {
		return trimmed.empty() || candidate.starts_with(trimmed);
	};

	if (line == 0 || (parent.empty() && indent.empty())) {
		if (startsLike("dynlex options:"))
			addItem("dynlex options:", "root config section", "dynlex options:");
		result.items = std::move(items);
		return result;
	}

	if (parent == "dynlex options") {
		const std::vector<std::pair<std::string, std::string>> suggestions = {
			{"import: \"\"", "import keyword"},
			{"comment: \"#\"", "comment prefix"},
			{"open section: \":\"", "section opener"},
			{"section: \"section\"", "section keyword"},
			{"function: \"function\"", "function keyword"},
			{"class:", "class settings"},
			{"flex: \"flex\"", "flex keyword"},
			{"local: \"local\"", "local keyword"},
			{"shorthand definitions:", "function declaration shorthands"},
			{"child sections:", "child section keywords"},
			{"messages:", "message overrides"},
		};
		for (const auto &[replacement, detail] : suggestions) {
			if (startsLike(replacement))
				addItem(replacement, detail, indent + replacement);
		}
	} else if (parent == "shorthand definitions") {
		const std::vector<std::pair<std::string, std::string>> suggestions = {
			{"action: \"to\"", "action declaration phrase"},
			{"value: \"to get\"", "value function declaration phrase"},
			{"replacement: \"means\"", "one-line replacement declaration phrase"},
		};
		for (const auto &[replacement, detail] : suggestions) {
			if (startsLike(replacement))
				addItem(replacement, detail, indent + replacement);
		}
	} else if (parent == "class") {
		const std::vector<std::pair<std::string, std::string>> suggestions = {
			{"name: \"class\"", "class keyword"},
			{"members: \"members\"", "members section keyword"},
		};
		for (const auto &[replacement, detail] : suggestions) {
			if (startsLike(replacement))
				addItem(replacement, detail, indent + replacement);
		}
	} else if (parent == "child sections") {
		const std::vector<std::pair<std::string, std::string>> suggestions = {
			{"execute: \"execute\"", "execute section keyword"},
			{"replacement: \"replacement\"", "replacement section keyword"},
			{"patterns: \"patterns\"", "patterns section keyword"},
			{"globals: \"globals\"", "globals section keyword"},
			{"before: \"before\"", "before section keyword"},
			{"after: \"after\"", "after section keyword"},
		};
		for (const auto &[replacement, detail] : suggestions) {
			if (startsLike(replacement))
				addItem(replacement, detail, indent + replacement);
		}
	} else if (parent == "messages") {
		const std::vector<std::pair<std::string, std::string>> suggestions = {
			{"could not import main file: \"couldn't import main file: {path}\"", "main import failure message"},
			{"failed to import source file: \"failed to import source file: {path}\"", "import failure message"},
			{"invalid indentation amount: \"Invalid indentation! expected {expected}, but found {found}\"",
			 "indentation size message"},
			{"invalid indentation character: \"Invalid indentation! expected only {expected}, but found {found}\"",
			 "indentation character message"},
			{"invalid indentation increase: \"Invalid indentation! expected at max {expected}, but found {found}\"",
			 "indentation increase message"},
			{"missing body section: \"Code without body section\"", "missing body section message"},
			{"unknown section: \"Unknown section: {section}\"", "unknown section message"},
			{"unexpected class line: \"unexpected line in class definition\"", "class line message"},
			{"shorthand definition requires pattern: \"Definition shorthand '{keyword}' requires a pattern\"",
			 "missing shorthand pattern message"},
			{"replacement shorthand requires inline body: \"'{keyword}' requires an expression on the same line\"",
			 "missing inline replacement body message"},
			{"value shorthand requires multiline body: \"'{keyword}' requires an indented body on following lines\"",
			 "inline value body message"},
			{"action shorthand returns value: \"Action '{function}' must return nothing\"", "action return contract message"},
			{"value shorthand returns nothing: \"Value function '{function}' must return a value\"",
			 "value return contract message"},
			{"replacement shorthand returns nothing: \"Replacement '{function}' must return a value\"",
			 "replacement return contract message"},
		};
		for (const auto &[replacement, detail] : suggestions) {
			if (startsLike(replacement))
				addItem(replacement, detail, indent + replacement);
		}
	}

	result.items = std::move(items);
	return result;
}

} // namespace lsp
