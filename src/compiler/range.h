#pragma once
#include <string_view>
struct CodeLine;
struct Section;
struct Range {
	CodeLine *line{};
	std::string_view subString;
	Range() = default;
	Range(CodeLine *line, int start, int end);
	Range(CodeLine *line, std::string_view subString);
	std::string toString() const;
	int start() const;
	int end() const;
	Range subRange(int start, int end);
	Section *section() const;
};