#pragma once
#include "sectionType.h"
#include "codeLine.h"
#include "patternDefinition.h"
#include <vector>
#include <list>
#include <string>
#include "patternReference.h"
struct ParseContext;
struct Variable;
struct Section
{
	inline Section(SectionType type, Section *parent = {}) : type(type), parent(parent)
	{
		if (parent)
			parent->children.push_back(this);
	}
	SectionType type;
	Section *parent{};
	std::vector<PatternDefinition *> patternDefinitions;
	std::vector<PatternReference *> patternReferences;
	std::vector<CodeLine *> codeLines;
	std::vector<Section *> children;
	std::unordered_map<std::string, Variable *> variables;
	// the start and end index of this section in compiled lines.
	int startLineIndex, endLineIndex;
	// this section is resolved it's pattern definitions are resolved.
	bool resolved = false;
	void collectPatternReferencesAndSections(std::list<PatternReference *> &patternReferences, std::list<Section *> &sections);
	virtual bool processLine(ParseContext &context, CodeLine *line);
	virtual Section *createSection(ParseContext &context, CodeLine *line);
	bool detectPatterns(ParseContext &context, Range range);
};