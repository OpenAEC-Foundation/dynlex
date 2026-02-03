#include "range.h"
#include "codeLine.h"
#include "sourceFile.h"

Range::Range(CodeLine *line, int start, int end) : line(line), subString(line->fullText.substr(start, end - start)) {}
Range::Range(CodeLine *line, std::string_view subString) : line(line), subString(subString) {}

std::string Range::toString() const {
	if (!line) {
		return "<unknown location>";
	}
	// link should be clickable in vs code
	// one-based index
	return line->sourceFile->uri + ":" + std::to_string(line->sourceFileLineIndex + 1) + ":" + std::to_string(start() + 1) +
		   "-" + std::to_string(end() + 1);
}

int Range::start() const { return subString.begin() - line->fullText.begin(); }

int Range::end() const { return subString.end() - line->fullText.begin(); }

Range Range::subRange(int start, int end) { return Range(line, subString.substr(start, end - start)); }

Section *Range::section() const { return line->section; }
