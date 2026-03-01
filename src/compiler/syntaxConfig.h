#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace lsp {
struct SourceFile;
}

struct ParseContext;

struct SyntaxConfig {
	std::string importKeyword = "import";
	std::string commentPrefix = "#";
	std::string sectionOpener = ":";

	std::string sectionName = "section";
	std::string functionName = "function";
	std::string className = "class";
	std::string macroName = "macro";
	std::string localName = "local";

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

size_t findCommentStart(std::string_view line, std::string_view commentPrefix);
std::optional<std::string_view> extractDirectiveArgument(std::string_view text, std::string_view keyword);
std::optional<std::string_view>
extractInlineSettingValue(std::string_view text, std::string_view key, std::string_view sectionOpener);
bool matchesConfiguredKeyword(std::string_view text, std::string_view keyword);
