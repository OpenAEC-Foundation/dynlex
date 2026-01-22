#pragma once
#include "patternElement.h"
#include <string>

struct SourceFile;
struct Section;
struct Expression;

struct CodeLine {
	CodeLine(std::string_view fullText, SourceFile *sourceFile) : sourceFile(sourceFile), fullText(fullText) {}

	// the source file in which this line resides
	SourceFile *sourceFile;

	// the line index in the source file
	int sourceFileLineIndex;

	// the line index after all source files are merged
	int mergedLineIndex;

	// the section in which this line resides
	Section *section{};
	// the section which this line starts
	Section *sectionOpening{};

	// full text including line terminators
	std::string_view fullText;
	// the text without comments and right-trimmed
	std::string_view rightTrimmedText{};
	// the pattern part of the line. excludes system patterns.
	std::string_view patternText{};

	// when resolved, this code line doesn't need to do any form of pattern matching.
	bool resolved{};

	// the elements of this code lines pattern
	std::vector<PatternElement> patternElements;

	// the expression tree for this code line (built during analysis)
	Expression *expression{};

	bool isPatternDefinition() const;
	bool isPatternReference() const;
};