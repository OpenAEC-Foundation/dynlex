#pragma once
#include "sectionType.h"
#include "codeLine.h"
#include "patternDefinition.h"
#include <vector>
#include <list>
#include <string>
#include "patternReference.h"
#include "stringHierarchy.h"
#include "variableReference.h"
struct ParseContext;
struct Variable;
struct Section
{
	inline Section(SectionType type, Section *parent = {}) : type(type), parent(parent)
	{
		if (parent) {
			parent->children.push_back(this);
		}
	}
	SectionType type;
	Section *parent{};
	std::vector<PatternDefinition *> patternDefinitions;
	std::vector<PatternReference *> patternReferences;
	std::unordered_map<std::string, std::vector<VariableReference *>> variableReferences;
	std::unordered_map<std::string, VariableReference *> variableDefinitions;
	std::vector<CodeLine *> codeLines;
	std::vector<Section *> children;
	std::unordered_map<std::string, Variable *> variables;
	// the start and end index of this section in compiled lines.
	int startLineIndex, endLineIndex;
	// count of unresolved pattern references + unresolved child sections
	int unresolvedCount = 0;
	// whether all pattern definitions in this section are resolved
	bool patternDefinitionsResolved = false;
	void collectPatternReferencesAndSections(std::list<PatternReference *> &patternReferences, std::list<Section *> &sections);
	virtual bool processLine(ParseContext &context, CodeLine *line);
	virtual Section *createSection(ParseContext &context, CodeLine *line);
	bool detectPatterns(ParseContext &context, Range range, SectionType patternType);
	bool detectPatternsRecursively(ParseContext& context, Range range, StringHierarchy* node, SectionType patternType);
	void addVariableReference(ParseContext& context, VariableReference* reference);
	void searchParentPatterns(ParseContext& context, VariableReference* reference);
	void addPatternReference(PatternReference* reference);
	void incrementUnresolved();
	void decrementUnresolved();
};