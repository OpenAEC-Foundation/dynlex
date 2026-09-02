#include "syntaxConfig.h"
#include "compiler.h"
#include "parseContext.h"
#include "pathUtils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <vector>

using namespace std::literals;

namespace {

std::string renderMessageTemplate(
	std::string_view templ, std::initializer_list<std::pair<std::string_view, std::string_view>> replacements
) {
	std::string result(templ);
	for (const auto &[key, value] : replacements) {
		std::string needle = "{" + std::string(key) + "}";
		size_t pos = 0;
		while ((pos = result.find(needle, pos)) != std::string::npos) {
			result.replace(pos, needle.size(), value);
			pos += value.size();
		}
	}
	return result;
}

struct ConfigNode {
	std::string key;
	std::optional<std::string> value;
	int lineNumber = 0;
	ConfigNode *parent{};
	std::vector<std::unique_ptr<ConfigNode>> children;
};

std::string trim(std::string_view text) {
	size_t start = 0;
	while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
		start++;
	size_t end = text.size();
	while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
		end--;
	return std::string(text.substr(start, end - start));
}

bool hasWhitespace(std::string_view text) {
	return std::any_of(text.begin(), text.end(), [](unsigned char c) {
		return std::isspace(c);
	});
}

std::optional<std::string> parseScalarValue(std::string_view text) {
	std::string trimmed = trim(text);
	if (trimmed.empty())
		return std::nullopt;
	if (trimmed.front() != '"')
		return trimmed;
	if (trimmed.size() < 2 || trimmed.back() != '"')
		return std::nullopt;

	std::string value;
	for (size_t i = 1; i + 1 < trimmed.size(); ++i) {
		char c = trimmed[i];
		if (c == '\\' && i + 1 < trimmed.size() - 1) {
			char next = trimmed[++i];
			switch (next) {
			case 'n':
				value += '\n';
				break;
			case 't':
				value += '\t';
				break;
			case 'r':
				value += '\r';
				break;
			case '"':
				value += '"';
				break;
			case '\\':
				value += '\\';
				break;
			default:
				value += next;
				break;
			}
			continue;
		}
		value += c;
	}
	return value;
}

void addConfigError(ParseContext &context, std::string_view path, int lineNumber, std::string message) {
	std::string fullMessage = "syntax config";
	if (!path.empty())
		fullMessage += " " + std::string(path);
	if (lineNumber > 0)
		fullMessage += ":" + std::to_string(lineNumber);
	fullMessage += ": " + message;
	context.diagnostics.push_back(Diagnostic::configParseError(fullMessage, Range()));
}

bool parseConfigTree(ParseContext &context, std::string_view path, std::string_view content, ConfigNode &root) {
	std::vector<ConfigNode *> stack = {&root};
	std::string indentUnit;
	int previousIndentLevel = 0;
	ConfigNode *previousNode = nullptr;
	size_t lineStart = 0;
	int lineNumber = 0;

	while (lineStart <= content.size()) {
		size_t lineEnd = content.find_first_of("\r\n", lineStart);
		if (lineEnd == std::string_view::npos)
			lineEnd = content.size();
		std::string_view line = content.substr(lineStart, lineEnd - lineStart);
		lineNumber++;

		if (lineEnd < content.size() && content[lineEnd] == '\r' && lineEnd + 1 < content.size() &&
			content[lineEnd + 1] == '\n')
			lineStart = lineEnd + 2;
		else
			lineStart = lineEnd + 1;

		size_t commentPos = findCommentStart(line, "#");
		if (commentPos != std::string_view::npos)
			line = line.substr(0, commentPos);

		size_t indentLength = 0;
		while (indentLength < line.size() && std::isspace(static_cast<unsigned char>(line[indentLength])))
			indentLength++;
		size_t lastNonWhitespace = line.find_last_not_of(" \t");
		if (lastNonWhitespace == std::string_view::npos)
			continue;
		line = line.substr(0, lastNonWhitespace + 1);

		std::string indent(std::string(line.substr(0, indentLength)));
		if (!indent.empty()) {
			char indentChar = indent.front();
			size_t invalidCharIndex = indent.find_first_not_of(indentChar);
			if (invalidCharIndex != std::string::npos) {
				addConfigError(context, path, lineNumber, "mixed indentation is not allowed in config.dl");
				return false;
			}
			if (indentUnit.empty())
				indentUnit = indent;
			if (indent.size() % indentUnit.size() != 0) {
				addConfigError(context, path, lineNumber, "indentation does not match the earlier config.dl indentation");
				return false;
			}
		}

		int indentLevel = indent.empty() ? 0 : static_cast<int>(indent.size() / indentUnit.size());
		if (indentLevel > previousIndentLevel + 1) {
			addConfigError(context, path, lineNumber, "indentation can only increase by one level at a time");
			return false;
		}
		if (indentLevel > previousIndentLevel && !previousNode) {
			addConfigError(context, path, lineNumber, "indented config entry must follow a parent entry");
			return false;
		}
		if (indentLevel > previousIndentLevel)
			stack.push_back(previousNode);
		else if (indentLevel < previousIndentLevel)
			stack.resize(static_cast<size_t>(indentLevel) + 1);
		previousIndentLevel = indentLevel;

		std::string_view trimmedLine = line.substr(indentLength);
		size_t colonPos = trimmedLine.find(':');
		if (colonPos == std::string_view::npos) {
			addConfigError(context, path, lineNumber, "expected 'key: value' or 'key:'");
			return false;
		}

		std::string key = trim(trimmedLine.substr(0, colonPos));
		if (key.empty()) {
			addConfigError(context, path, lineNumber, "config key cannot be empty");
			return false;
		}

		std::optional<std::string> value = parseScalarValue(trimmedLine.substr(colonPos + 1));
		if (!trim(trimmedLine.substr(colonPos + 1)).empty() && !value.has_value()) {
			addConfigError(context, path, lineNumber, "invalid config value; use a quoted string or a single bare word");
			return false;
		}

		auto node = std::make_unique<ConfigNode>();
		node->key = std::move(key);
		node->value = std::move(value);
		node->lineNumber = lineNumber;
		node->parent = stack.back();
		previousNode = node.get();
		stack.back()->children.push_back(std::move(node));
	}

	return true;
}

std::string nodePath(const ConfigNode &node) {
	std::vector<std::string> parts;
	for (const ConfigNode *current = &node; current && current->parent; current = current->parent)
		parts.push_back(current->key);
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
		if (child->key == key)
			return child.get();
	}
	return nullptr;
}

bool assignNameValue(
	ParseContext &context, std::string_view path, const ConfigNode &node, std::string &target, bool allowChildren = false,
	bool requireValue = true, bool allowWhitespace = false
) {
	const ConfigNode *nameChild = findChild(node, "name");
	if (node.value && nameChild) {
		addConfigError(
			context, path, node.lineNumber,
			"config entry '" + nodePath(node) + "' cannot define both a direct value and a nested name"
		);
		return false;
	}
	const std::string *value = node.value ? &*node.value : (nameChild && nameChild->value ? &*nameChild->value : nullptr);
	if (!value) {
		if (!requireValue)
			return true;
		addConfigError(context, path, node.lineNumber, "config entry '" + nodePath(node) + "' is missing a name");
		return false;
	}
	if (!allowWhitespace && hasWhitespace(*value)) {
		addConfigError(context, path, node.lineNumber, "config entry '" + nodePath(node) + "' must be a single token");
		return false;
	}
	if (allowWhitespace) {
		bool invalidSpacing = value->empty() || value->front() == ' ' || value->back() == ' ' || value->contains("  ") ||
							  std::any_of(value->begin(), value->end(), [](unsigned char character) {
			return std::isspace(character) && character != ' ';
		});
		if (invalidSpacing) {
			addConfigError(
				context, path, node.lineNumber,
				"config entry '" + nodePath(node) + "' must contain tokens separated by single spaces"
			);
			return false;
		}
	}
	target = *value;
	if (!allowChildren) {
		for (const auto &child : node.children) {
			if (child.get() != nameChild) {
				addConfigError(
					context, path, child->lineNumber,
					"unknown nested config key '" + child->key + "' under '" + nodePath(node) + "'"
				);
				return false;
			}
		}
	}
	return true;
}

bool applySyntaxNode(ParseContext &context, std::string_view path, const ConfigNode &node, SyntaxConfig &config) {
	if (node.key == "import") {
		return assignNameValue(context, path, node, config.importKeyword);
	}
	if (node.key == "comment") {
		if (!node.value) {
			addConfigError(context, path, node.lineNumber, "config entry 'comment' must define a value");
			return false;
		}
		config.commentPrefix = *node.value;
		return true;
	}
	if (node.key == "open section") {
		if (!node.value) {
			addConfigError(context, path, node.lineNumber, "config entry 'open section' must define a value");
			return false;
		}
		config.sectionOpener = *node.value;
		return true;
	}
	if (node.key == "section") {
		return assignNameValue(context, path, node, config.sectionName);
	}
	if (node.key == "function") {
		return assignNameValue(context, path, node, config.functionName);
	}
	if (node.key == "convert") {
		return assignNameValue(context, path, node, config.conversionName);
	}
	if (node.key == "implicitly") {
		return assignNameValue(context, path, node, config.implicitName);
	}
	if (node.key == "flex") {
		return assignNameValue(context, path, node, config.flexName);
	}
	if (node.key == "local") {
		return assignNameValue(context, path, node, config.localName);
	}
	if (node.key == "exposed") {
		return assignNameValue(context, path, node, config.exposedName);
	}
	if (node.key == "shorthand definitions") {
		if (node.value) {
			addConfigError(context, path, node.lineNumber, "config entry 'shorthand definitions' cannot define a value");
			return false;
		}
		for (const auto &child : node.children) {
			std::string *target = nullptr;
			if (child->key == "action")
				target = &config.actionShorthand;
			else if (child->key == "value")
				target = &config.valueShorthand;
			else if (child->key == "replacement")
				target = &config.replacementShorthand;
			else {
				addConfigError(
					context, path, child->lineNumber, "unknown config key '" + child->key + "' under 'shorthand definitions'"
				);
				return false;
			}
			if (!assignNameValue(context, path, *child, *target, false, true, true))
				return false;
		}
		return true;
	}
	if (node.key == "class") {
		if (!assignNameValue(context, path, node, config.className, true, false))
			return false;
		const ConfigNode *nameChild = findChild(node, "name");
		for (const auto &child : node.children) {
			if (child.get() == nameChild)
				continue;
			if (child->key == "members") {
				if (!assignNameValue(context, path, *child, config.membersSectionName))
					return false;
				continue;
			}
			if (child->key == "retain") {
				if (!assignNameValue(context, path, *child, config.retainSectionName))
					return false;
				continue;
			}
			if (child->key == "release") {
				if (!assignNameValue(context, path, *child, config.releaseSectionName))
					return false;
				continue;
			}
			if (child->key == "alignment") {
				if (!assignNameValue(context, path, *child, config.alignmentName))
					return false;
				continue;
			}
			if (child->key == "padding") {
				if (!assignNameValue(context, path, *child, config.paddingName))
					return false;
				continue;
			}
			addConfigError(context, path, child->lineNumber, "unknown config key '" + child->key + "' under 'class'");
			return false;
		}
		return true;
	}
	if (node.key == "messages") {
		for (const auto &child : node.children) {
			if (child->value) {
				config.messages.set(child->key, "message", *child->value);
			} else {
				for (const auto &grandChild : child->children) {
					if (!grandChild->value) {
						addConfigError(
							context, path, grandChild->lineNumber,
							"config entry 'messages." + child->key + "." + grandChild->key + "' must define a value"
						);
						return false;
					}
					config.messages.set(child->key, grandChild->key, *grandChild->value);
				}
			}
		}
		return true;
	}
	if (node.key == "child sections") {
		for (const auto &child : node.children) {
			if (child->key == "execute") {
				if (!assignNameValue(context, path, *child, config.executeSectionName))
					return false;
			} else if (child->key == "replacement") {
				if (!assignNameValue(context, path, *child, config.replacementSectionName))
					return false;
			} else if (child->key == "patterns") {
				if (!assignNameValue(context, path, *child, config.patternsSectionName))
					return false;
			} else if (child->key == "globals") {
				if (!assignNameValue(context, path, *child, config.globalsSectionName))
					return false;
			} else if (child->key == "before") {
				if (!assignNameValue(context, path, *child, config.beforeSectionName))
					return false;
			} else if (child->key == "after") {
				if (!assignNameValue(context, path, *child, config.afterSectionName))
					return false;
			} else {
				addConfigError(
					context, path, child->lineNumber, "unknown config key '" + child->key + "' under 'child sections'"
				);
				return false;
			}
		}
		return true;
	}

	addConfigError(context, path, node.lineNumber, "unknown config key '" + node.key + "'");
	return false;
}

bool loadSyntaxConfigFile(ParseContext &context, const std::string &path, SyntaxConfig &config) {
	lsp::SourceFile *file = context.fileSystem->getFile(path);
	if (!file)
		return true;

	ConfigNode root;
	if (!parseConfigTree(context, path, file->content, root))
		return false;
	if (root.children.empty()) {
		addConfigError(context, path, 0, "config.dl is empty");
		return false;
	}
	if (root.children.size() != 1 || root.children[0]->key != "dynlex options") {
		addConfigError(context, path, root.children[0]->lineNumber, "config.dl must start with 'dynlex options:'");
		return false;
	}

	for (const auto &child : root.children[0]->children) {
		if (!applySyntaxNode(context, path, *child, config))
			return false;
	}
	if (std::optional<std::string> error = definitionShorthandSyntaxError(config)) {
		const ConfigNode *shorthandNode = findChild(*root.children[0], "shorthand definitions");
		addConfigError(context, path, shorthandNode ? shorthandNode->lineNumber : root.children[0]->lineNumber, *error);
		return false;
	}
	return true;
}

std::string findProjectSyntaxConfigPath(ParseContext &context, const std::string &mainPath) {
	std::filesystem::path current = std::filesystem::absolute(pathutil::toFilesystemPath(mainPath)).parent_path();
	for (;;) {
		std::filesystem::path candidate = current / "config.dl";
		if (context.fileSystem->getFile(candidate.string()))
			return candidate.string();
		if (current == current.root_path())
			break;
		current = current.parent_path();
	}
	return {};
}

} // namespace

std::optional<std::string> definitionShorthandSyntaxError(const SyntaxConfig &config) {
	if (config.actionShorthand == config.valueShorthand || config.actionShorthand == config.replacementShorthand ||
		config.valueShorthand == config.replacementShorthand)
		return "shorthand definition phrases must be distinct";

	auto firstToken = [](std::string_view phrase) {
		return phrase.substr(0, phrase.find(' '));
	};
	const std::vector<std::string_view> definitionLeadingKeywords = {
		config.functionName, config.conversionName, config.sectionName, config.className,
		config.flexName,	 config.localName,		config.exposedName, config.implicitName,
	};
	const std::vector<std::string_view> childSectionKeywords = {
		config.executeSectionName, config.replacementSectionName, config.patternsSectionName, config.globalsSectionName,
		config.beforeSectionName,  config.afterSectionName,		  config.membersSectionName,  config.retainSectionName,
		config.releaseSectionName, config.alignmentName,		  config.paddingName,
	};
	auto conflictMessage = [](std::string_view phrase, std::string_view keyword) {
		return "shorthand definition phrase '" + std::string(phrase) + "' conflicts with structural keyword '" +
			   std::string(keyword) + "'";
	};
	for (std::string_view phrase : {std::string_view(config.actionShorthand), std::string_view(config.valueShorthand)}) {
		for (std::string_view keyword : definitionLeadingKeywords) {
			if (firstToken(phrase) == keyword)
				return conflictMessage(phrase, keyword);
		}
		for (std::string_view keyword : childSectionKeywords) {
			if (phrase == keyword)
				return conflictMessage(phrase, keyword);
		}
	}
	for (std::string_view keyword : childSectionKeywords) {
		if (config.replacementShorthand == keyword)
			return conflictMessage(config.replacementShorthand, keyword);
	}
	return std::nullopt;
}

bool initializeSyntaxConfigs(ParseContext &context, const std::string &mainPath) {
	context.builtinSyntax = {};
	std::vector<std::string> builtinCandidates = {
#ifdef DYNLEX_WEB
		"/lib/config.dl",
		"lib/config.dl",
#endif
		std::string(PROJECT_SOURCE_DIR) + "/lib/config.dl",
		"/usr/share/dynlex/lib/config.dl",
	};
	for (const std::string &candidate : builtinCandidates) {
		if (!context.fileSystem->getFile(candidate))
			continue;
		if (!loadSyntaxConfigFile(context, candidate, context.builtinSyntax))
			return false;
		break;
	}

	context.projectSyntax = context.builtinSyntax;
	context.projectSyntaxConfigPath = findProjectSyntaxConfigPath(context, mainPath);
	if (!context.projectSyntaxConfigPath.empty() &&
		!loadSyntaxConfigFile(context, context.projectSyntaxConfigPath, context.projectSyntax))
		return false;

	return true;
}

const SyntaxConfig &syntaxConfigForSourcePath(const ParseContext &context, std::string_view path) {
	return isInternalSourcePath(path) ? context.builtinSyntax : context.projectSyntax;
}

const SyntaxConfig &syntaxConfigForSourceFile(const ParseContext &context, const lsp::SourceFile *sourceFile) {
	if (!sourceFile)
		return context.projectSyntax;
	return syntaxConfigForSourcePath(context, sourceFile->uri);
}

const SyntaxConfig &syntaxConfigForRange(const ParseContext &context, Range range) {
	if (!range.line || !range.line->sourceFile)
		return context.projectSyntax;
	return syntaxConfigForSourceFile(context, range.line->sourceFile);
}

size_t findCommentStart(std::string_view line, std::string_view commentPrefix) {
	if (commentPrefix.empty())
		return std::string_view::npos;

	bool inString = false;
	for (size_t i = 0; i < line.size(); ++i) {
		char c = line[i];
		if (c == '"' && (i == 0 || line[i - 1] != '\\')) {
			inString = !inString;
			continue;
		}
		if (!inString && i + commentPrefix.size() <= line.size() && line.substr(i, commentPrefix.size()) == commentPrefix) {
			return i;
		}
	}
	return std::string_view::npos;
}

std::optional<std::string_view> extractDirectiveArgument(std::string_view text, std::string_view keyword) {
	if (!text.starts_with(keyword))
		return std::nullopt;
	std::string_view rest = text.substr(keyword.size());
	if (rest.empty() || !std::isspace(static_cast<unsigned char>(rest.front())))
		return std::nullopt;
	size_t start = 0;
	while (start < rest.size() && std::isspace(static_cast<unsigned char>(rest[start])))
		start++;
	rest.remove_prefix(start);
	return rest.empty() ? std::nullopt : std::optional<std::string_view>(rest);
}

std::optional<std::string_view>
extractInlineSettingValue(std::string_view text, std::string_view key, std::string_view sectionOpener) {
	if (!text.starts_with(key))
		return std::nullopt;
	text.remove_prefix(key.size());
	if (!text.starts_with(sectionOpener))
		return std::nullopt;
	text.remove_prefix(sectionOpener.size());
	size_t start = 0;
	while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
		start++;
	text.remove_prefix(start);
	return text;
}

bool matchesConfiguredKeyword(std::string_view text, std::string_view keyword) { return text == keyword; }

SyntaxConfig::Messages::Messages() {
	set("could not import main file", "message", "couldn't import main file: {path}");
	set("failed to import source file", "message", "failed to import source file: {path}");
	set("invalid indentation amount", "message", "Invalid indentation! expected {expected}, but found {found}");
	set("invalid indentation character", "message", "Invalid indentation! expected only {expected}, but found {found}");
	set("invalid indentation increase", "message", "Invalid indentation! expected at max {expected}, but found {found}");
	set("missing body section", "message", "Code without body section");
	set("unknown section", "message", "Unknown section: {section}");
	set("unexpected class line", "message", "unexpected line in class definition");
	set("integer literal out of range", "message", "Integer literal exceeds the signed 64-bit range");
	set("floating point literal out of range", "message", "Floating-point literal exceeds the supported range");
	set("invalid numeric literal", "message", "Invalid numeric literal");
	set("function has missing return path", "message", "Function '{function}' does not return a value on every reachable path");
	set("multiple reachable return types", "message",
		"Function has multiple reachable return types: {first_type} and {second_type}");
	set("conversion requires one parameter", "message",
		"A conversion must contain exactly one parameter and no other pattern text");
	set("implicit modifier requires conversion", "message", "The '{modifier}' modifier can only be used with '{conversion}'");
	set("shorthand definition requires pattern", "message", "Definition shorthand '{keyword}' requires a pattern");
	set("replacement shorthand requires inline body", "message", "'{keyword}' requires an expression on the same line");
	set("value shorthand requires multiline body", "message", "'{keyword}' requires an indented body on following lines");
	set("action shorthand returns value", "message", "Action '{function}' must return nothing");
	set("value shorthand returns nothing", "message", "Value function '{function}' must return a value");
	set("replacement shorthand returns nothing", "message", "Replacement '{function}' must return a value");
	set("ambiguous conversion", "message", "More than one conversion from {from_type} to {to_type} is equally specific");
	set("address of requires addressable value", "message", "address of requires an addressable value");
	set("store at value incompatible", "message", "store at cannot store {value_type} through a pointer to {element_type}");
}

const std::string *SyntaxConfig::Messages::find(std::string_view key, std::string_view variant) const {
	auto keyIt = entries.find(std::string(key));
	if (keyIt == entries.end())
		return nullptr;
	auto variantIt = keyIt->second.find(std::string(variant));
	if (variantIt != keyIt->second.end())
		return &variantIt->second;
	if (variant != "message") {
		variantIt = keyIt->second.find("message");
		if (variantIt != keyIt->second.end())
			return &variantIt->second;
	}
	return nullptr;
}

void SyntaxConfig::Messages::set(std::string key, std::string variant, std::string value) {
	entries[std::move(key)][std::move(variant)] = std::move(value);
}

std::string
renderSyntaxMessage(std::string_view templ, std::initializer_list<std::pair<std::string_view, std::string_view>> replacements) {
	return renderMessageTemplate(templ, replacements);
}

std::string renderConfiguredMessage(const SyntaxConfig &syntaxConfig, std::string_view key) {
	return renderConfiguredMessage(syntaxConfig, key, "message");
}

std::string renderConfiguredMessage(const SyntaxConfig &syntaxConfig, std::string_view key, std::string_view variant) {
	return renderConfiguredMessage(syntaxConfig, key, variant, {});
}

std::string renderConfiguredMessage(
	const SyntaxConfig &syntaxConfig, std::string_view key, std::string_view variant,
	std::initializer_list<std::pair<std::string_view, std::string_view>> replacements
) {
	const std::string *templ = syntaxConfig.messages.find(key, variant);
	if (!templ)
		return std::string(key);
	return renderMessageTemplate(*templ, replacements);
}

std::string renderConfiguredMessage(
	const SyntaxConfig &syntaxConfig, std::string_view key, std::string_view variant,
	const std::vector<std::pair<std::string, std::string>> &replacements
) {
	const std::string *templ = syntaxConfig.messages.find(key, variant);
	if (!templ)
		return std::string(key);
	std::string result(*templ);
	for (const auto &[placeholderName, placeholderValue] : replacements) {
		std::string needle = "{" + placeholderName + "}";
		size_t pos = 0;
		while ((pos = result.find(needle, pos)) != std::string::npos) {
			result.replace(pos, needle.size(), placeholderValue);
			pos += placeholderValue.size();
		}
	}
	return result;
}
