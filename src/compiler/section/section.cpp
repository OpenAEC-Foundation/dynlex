#include "section.h"
#include "effectSection.h"
#include "parseContext.h"
#include "patternTreeNode.h"

void Section::collectPatternReferencesAndSections(
	std::list<PatternReference *> &patternReferences, std::list<Section *> &sections
) {
	patternReferences.insert(patternReferences.end(), this->patternReferences.begin(), this->patternReferences.end());
	sections.push_back(this);
	for (auto child : children) {
		child->collectPatternReferencesAndSections(patternReferences, sections);
	}
}

bool Section::processLine(ParseContext &context, CodeLine *line) {
	return detectPatterns(context, Range(line, line->patternText));
}

Section *Section::createSection(ParseContext &, CodeLine *line) {
	// determine the section type
	std::size_t spaceIndex = line->patternText.find(' ');
	Section *newSection{};
	if (spaceIndex != std::string::npos) {
		std::string sectionTypeString = (std::string)line->patternText.substr(0, spaceIndex);
		if (sectionTypeString == "effect") {
			newSection = new EffectSection(this);
		}
		if (newSection) {
			// check if there's a pattern right after the name (f.e. "effect set val to var" <- right after "effect ")
			std::string_view sectionPatternString = line->patternText.substr(spaceIndex + 1);
			if (sectionPatternString.length()) {
				newSection->patternDefinitions.push_back(new PatternDefinition(Range(line, sectionPatternString)));
			}
		}
	}
	if (!newSection) {
		// custom section
		newSection = new Section(SectionType::Custom, this);
		patternReferences.push_back(new PatternReference(Range(line, line->patternText), SectionType::Section));
	}
	return newSection;
}

bool Section::detectPatterns(ParseContext &, Range range) {
	// treat as normal code
	// recognize intrinsic calls, numbers, strings and braces and add pattern references for them
	// add pattern references
	patternReferences.push_back(new PatternReference(range, SectionType::Effect));
	return true;
}
