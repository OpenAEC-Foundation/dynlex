#pragma once
#include <cstddef>
#include <optional>
#include <string_view>

struct ParseContext;

struct TopLevelSectionOpener {
	size_t offset{};
	size_t bodyOffset{};
};

std::optional<TopLevelSectionOpener> findTopLevelSectionOpener(std::string_view text, std::string_view sectionOpener);
bool expandDefinitionShorthands(ParseContext &context);
