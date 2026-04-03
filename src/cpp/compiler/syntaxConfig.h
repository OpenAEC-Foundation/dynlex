#pragma once
#include "range.h"
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lsp {
struct SourceFile;
}

struct ParseContext;

struct SyntaxConfig {
	struct Messages {
		std::unordered_map<std::string, std::unordered_map<std::string, std::string>> entries;

		Messages();
		const std::string *find(std::string_view key, std::string_view variant = "message") const;
		void set(std::string key, std::string variant, std::string value);
	} messages;

	std::string importKeyword = "import";
	std::string commentPrefix = "#";
	std::string sectionOpener = ":";

	std::string sectionName = "section";
	std::string functionName = "function";
	std::string className = "class";
	std::string flexName = "flex";
	std::string localName = "local";
	std::string exposedName = "exposed";

	std::string executeSectionName = "execute";
	std::string replacementSectionName = "replacement";
	std::string patternsSectionName = "patterns";
	std::string membersSectionName = "members";
	std::string globalsSectionName = "globals";
	std::string beforeSectionName = "before";
	std::string afterSectionName = "after";
	std::string precedenceDefaultName = "default";
	std::string alignmentName = "alignment";
};

bool initializeSyntaxConfigs(ParseContext &context, const std::string &mainPath);
const SyntaxConfig &syntaxConfigForSourcePath(const ParseContext &context, std::string_view path);
const SyntaxConfig &syntaxConfigForSourceFile(const ParseContext &context, const lsp::SourceFile *sourceFile);
const SyntaxConfig &syntaxConfigForRange(const ParseContext &context, Range range);

size_t findCommentStart(std::string_view line, std::string_view commentPrefix);
std::optional<std::string_view> extractDirectiveArgument(std::string_view text, std::string_view keyword);
std::optional<std::string_view>
extractInlineSettingValue(std::string_view text, std::string_view key, std::string_view sectionOpener);
bool matchesConfiguredKeyword(std::string_view text, std::string_view keyword);
std::string
renderSyntaxMessage(std::string_view templ, std::initializer_list<std::pair<std::string_view, std::string_view>> replacements);
std::string renderConfiguredMessage(const SyntaxConfig &syntaxConfig, std::string_view key);
std::string renderConfiguredMessage(const SyntaxConfig &syntaxConfig, std::string_view key, std::string_view variant);
std::string renderConfiguredMessage(
	const SyntaxConfig &syntaxConfig, std::string_view key, std::string_view variant,
	std::initializer_list<std::pair<std::string_view, std::string_view>> replacements
);
std::string renderConfiguredMessage(
	const SyntaxConfig &syntaxConfig, std::string_view key, std::string_view variant,
	const std::vector<std::pair<std::string, std::string>> &replacements
);
