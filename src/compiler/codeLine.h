#pragma once
#include "patternElement.h"
#include <string>
#include <vector>

namespace lsp {
struct SourceFile;
}
struct Section;
struct Expression;

struct SourceLocation {
	lsp::SourceFile *sourceFile{};
	int sourceFileLineIndex{};
	int column{};
};

struct SourceSlice {
	int transformedStart{};
	int transformedEnd{};
	lsp::SourceFile *sourceFile{};
	int sourceFileLineIndex{};
	int sourceColumnStart{};
};

struct CodeLine {
	CodeLine(std::string_view fullText, lsp::SourceFile *sourceFile) : sourceFile(sourceFile), fullText(fullText) {}

	// the source file in which this line resides
	lsp::SourceFile *sourceFile;

	// the line index in the source file
	int sourceFileLineIndex;

	// the line index after all source files are merged
	int mergedLineIndex;

	// the section in which this line resides
	Section *section{};
	// the section which this line starts
	Section *sectionOpening{};

	// full text including line terminators
	std::string ownedText;
	std::string_view fullText;
	// the text without comments and right-trimmed
	std::string_view rightTrimmedText{};
	// the pattern part of the line. excludes system patterns.
	std::string_view patternText{};
	// extra logical indentation levels injected by preprocessing for one-line sections
	int logicalIndentOffset = 0;
	// whether this logical line reuses a physical indent prefix from another source line
	bool hasIndentOverride = false;
	std::string indentOverride{};

	// when resolved, this code line doesn't need to do any form of pattern matching.
	bool resolved{};

	// the elements of this code lines pattern
	std::vector<PatternElement> patternElements;

	// the expression tree for this code line (built during analysis)
	Expression *expression{};

	// Mapping from transformed text back to original file locations.
	std::vector<SourceSlice> sourceSlices;

	bool isPatternDefinition() const;
	bool isPatternReference() const;
	void setOwnedText(std::string text);
	SourceLocation mapOffsetToSource(int offset) const;
	int mapSourceToOffset(const std::string &uri, int sourceLineIndex, int column) const;
	bool containsSourceLocation(const std::string &uri, int sourceLineIndex, int column) const;
};
