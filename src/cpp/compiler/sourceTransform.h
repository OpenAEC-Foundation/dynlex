#pragma once
#include "IndentData.h"
#include <cstddef>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ParseContext;
struct CodeLine;
namespace lsp {
struct SourceFile;
}

struct SourceTransformState {
	IndentData indentation;
	std::unordered_map<lsp::SourceFile *, std::vector<int>> activeByFile;
};

struct TopLevelSectionOpener {
	size_t offset{};
	size_t bodyOffset{};
};

std::optional<TopLevelSectionOpener> findTopLevelSectionOpener(std::string_view text, std::string_view sectionOpener);
bool expandDefinitionShorthands(ParseContext &context, SourceTransformState &state);
CodeLine *expandFormattedStringLine(ParseContext &context, CodeLine &line);
