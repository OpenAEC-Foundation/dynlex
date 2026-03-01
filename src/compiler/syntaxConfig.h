#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace lsp {
struct SourceFile;
}

struct ParseContext;

struct SyntaxConfig {
	struct Messages {
		std::string couldNotImportMainFile = "couldn't import main file: {path}";
		std::string failedToImportSourceFile = "failed to import source file: {path}";
		std::string invalidIndentationAmount = "Invalid indentation! expected {expected}, but found {found}";
		std::string invalidIndentationCharacter = "Invalid indentation! expected only {expected}, but found {found}";
		std::string invalidIndentationIncrease = "Invalid indentation! expected at max {expected}, but found {found}";
		std::string missingBodySection = "Code without body section";
		std::string unknownSection = "Unknown section: {section}";
		std::string unexpectedClassLine = "unexpected line in class definition";
	} messages;

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
std::string
renderSyntaxMessage(std::string_view templ, std::initializer_list<std::pair<std::string_view, std::string_view>> replacements);
