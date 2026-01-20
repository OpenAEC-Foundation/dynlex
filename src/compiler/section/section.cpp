#include "section.h"
#include "effectSection.h"
#include "expressionSection.h"
#include "parseContext.h"
#include "patternTreeNode.h"
#include "stringHierarchy.h"
#include <stack>
using namespace std::literals;

void Section::collectPatternReferencesAndSections(
	std::list<PatternReference *> &patternReferences, std::list<Section *> &sections
) {
	patternReferences.insert(patternReferences.end(), this->patternReferences.begin(), this->patternReferences.end());
	if (this->patternDefinitions.size())
		sections.push_back(this);
	for (Section *child : children) {
		child->collectPatternReferencesAndSections(patternReferences, sections);
	}
}

bool Section::processLine(ParseContext &context, CodeLine *line) {
	return detectPatterns(context, Range(line, line->patternText), SectionType::Effect);
}

Section *Section::createSection(ParseContext &context, CodeLine *line) {
	// determine the section type
	std::size_t spaceIndex = line->patternText.find(' ');
	Section *newSection{};
	if (spaceIndex != std::string::npos) {
		std::string sectionTypeString = (std::string)line->patternText.substr(0, spaceIndex);
		if (sectionTypeString == "effect") {
			newSection = new EffectSection(this);
		} else if (sectionTypeString == "expression") {
			newSection = new ExpressionSection(this);
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
		detectPatterns(context, Range(line, line->patternText), SectionType::Section);
		addPatternReference(new PatternReference(Range(line, line->patternText), SectionType::Section));
	}
	return newSection;
}

StringHierarchy *createHierarchy(ParseContext &context, Range range) {
	std::stack<StringHierarchy *> nodeStack;
	StringHierarchy *base = new StringHierarchy(0, 0);
	nodeStack.push(base);

	for (size_t index = 0; index < range.subString.size(); index++) {
		char charachter = range.subString[index];

		auto push = [&nodeStack, index, charachter] {
			StringHierarchy *newChild = new StringHierarchy(charachter, index + 1);
			nodeStack.top()->children.push_back(newChild);
			nodeStack.push(newChild);
		};
		auto tryPop = [&nodeStack, &context, &range, base, index, charachter](char requiredCharachter) {
			if (nodeStack.top()->charachter == requiredCharachter) {
				nodeStack.top()->end = index;
				nodeStack.pop();
				return true;
			} else {
				delete base;
				context.diagnostics.push_back(Diagnostic(
					Diagnostic::Level::Error, std::string("unmatched closing charachter found: '") + charachter + "'",
					Range(range.line, range.subString.substr(index, 1))
				));
				return false;
			}
		};

		switch (charachter) {
		case '(': {
			push();
			break;
		}
		case ')': {
			if (nodeStack.top()->charachter == ',') {
				nodeStack.top()->end = index;
				nodeStack.pop();
			}
			if (!tryPop('('))
				return nullptr;
			break;
		}
		case '"': {
			push();
			auto stringIt = range.subString.begin() + index;
			while (true) {
				stringIt = std::find(stringIt + 1, range.subString.end(), '\"');
				if (stringIt == range.subString.end()) {
					context.diagnostics.push_back(Diagnostic(
						Diagnostic::Level::Error, std::string("unmatched string charachter found: '\"'"),
						Range(range.line, range.subString.substr(index, 1))
					));
					delete base;
					return nullptr;
				}
				if (*(stringIt - 1) != '\\') {
					index = stringIt - range.subString.begin();
					break;
				}
			};
			nodeStack.top()->end = index;
			nodeStack.pop();
			break;
		}
		case '\\': {
			if (nodeStack.top()->charachter == '"')
				// skip the next charachter
				index++;
			break;
		}
		case ',': {
			if (nodeStack.top()->charachter == '(') {
				// add the child, don't push
				StringHierarchy *newChild = new StringHierarchy(charachter, nodeStack.top()->start);
				// move all other children to this new child
				newChild->children = nodeStack.top()->children;
				newChild->end = index;
				nodeStack.top()->children = {newChild};
				// add another ',' child and push
				push();
			} else if (nodeStack.top()->charachter == ',') {
				nodeStack.top()->end = index;
				nodeStack.pop();
				push();
			} else {
				context.diagnostics.push_back(Diagnostic(
					Diagnostic::Level::Error, std::string("found comma without enclosing braces"),
					Range(range.line, range.subString.substr(index, 1))
				));
				delete base;
				return nullptr;
			}
			break;
		}
		default:
			break;
		}
	}
	if (nodeStack.size() > 1) {
		while (nodeStack.size() > 1) {
			context.diagnostics.push_back(Diagnostic(
				Diagnostic::Level::Error, "unmatched closing charachter found: '"s + nodeStack.top()->charachter + "'",
				range.subRange(nodeStack.top()->start, nodeStack.top()->start + 1)
			));
			nodeStack.pop();
		}
		delete base;
		return nullptr;
	}

	base->end = range.end();
	return base;
}

bool Section::detectPatterns(ParseContext &context, Range range, SectionType patternType) {
	StringHierarchy *hierarchy = createHierarchy(context, range);
	if (!hierarchy)
		return false;
	detectPatternsRecursively(context, range, hierarchy, patternType);
	delete hierarchy;
	return true;
}

bool Section::detectPatternsRecursively(ParseContext &context, Range range, StringHierarchy *node, SectionType patternType) {
	Range relativeRange = Range(range.line, range.subString.substr(node->start, node->end - node->start));
	// treat as normal code
	// recognize intrinsic calls, numbers, strings and braces and add pattern references for them
	PatternReference *reference = new PatternReference(relativeRange, patternType);

	auto delegate = [this, &context, &range](StringHierarchy *childNode) {
		return detectPatternsRecursively(
			context, range.subRange(childNode->start, childNode->end), childNode->cloneWithOffset(-childNode->start),
			SectionType::Expression
		);
	};

	for (StringHierarchy *child : node->children) {
		if (child->charachter == '(') {
			// detect subpatterns
			if (child->children.size() && child->children[0]->charachter == ',') {
				for (StringHierarchy *subChild : child->children) {

					if (!delegate(subChild))
						return false;
				}
			} else {
				if (!delegate(child))
					return false;
			}
			constexpr std::string_view intrinsicPrefix = "@intrinsic("sv;
			if (child->start >= intrinsicPrefix.length() &&
				range.subString.substr(child->start - intrinsicPrefix.length(), intrinsicPrefix.length()) == intrinsicPrefix) {
				reference->pattern.replaceLine(child->start - intrinsicPrefix.length(), child->end + ")"sv.length());
				// this is an intrinsic call
			} else {
				reference->pattern.replaceLine(child->start - "("sv.length(), child->end + ")"sv.length());
			}
		} else if (child->charachter == '"') {
			reference->pattern.replaceLine(child->start - "\""sv.length(), child->end + "\""sv.length());
		}
		// when it's a string, we don't have to do anything for now
	}

	// search for other replacables
	// we don't need to capture negative values (-) because they can be captured using a pattern
	std::regex numberRegex = std::regex("\\d+(?:\\.\\d)?");

	std::cregex_iterator iter(range.subString.begin(), range.subString.end(), numberRegex);
	std::cregex_iterator end;
	for (; iter != end; ++iter) {
		reference->pattern.replaceLine(iter->position(), iter->position() + iter->length());
	}

	auto addWhiteSpaceWarning = [&context, &range, &reference](size_t start, size_t end) {
		context.diagnostics.push_back(Diagnostic(
			Diagnostic::Level::Warning, "all whitespace in patterns should be a single space",
			range.subRange(reference->pattern.getLinePos(start), reference->pattern.getLinePos(end))
		));
	};

	std::smatch matches;

	// also, trim the pattern. we can't just use trim(), as that prevents diagnostics and does not update keyframes.

	// left
	std::regex_search(reference->pattern.text, matches, std::regex("^(\\s*)"));
	std::string leftWhiteSpace = matches[0];
	if (!leftWhiteSpace.empty()) {
		if (leftWhiteSpace != " ") {
			addWhiteSpaceWarning(0, leftWhiteSpace.size());
		}
		reference->pattern.replacePattern(0, leftWhiteSpace.size(), "");
	}

	// right
	std::regex_search(reference->pattern.text, matches, std::regex("(\\s*)$"));
	std::string rightWhiteSpace = matches[0];
	if (!rightWhiteSpace.empty()) {
		if (rightWhiteSpace != " ") {
			addWhiteSpaceWarning(matches.position(), reference->pattern.text.size());
		}
		reference->pattern.replacePattern(0, rightWhiteSpace.size(), "");
	}

	// now, check if there's some extra space.
	// match any whitespace except for ' '
	std::regex spaceRegex = std::regex("\\s{2,}|[^\\S ]");

	size_t lastIndex = 0;
	std::cmatch charMatches;
	while (std::regex_search(
		reference->pattern.text.c_str() + lastIndex, reference->pattern.text.c_str() + reference->pattern.text.size(),
		charMatches, spaceRegex
	)) {
		size_t matchPos = lastIndex + charMatches.position();
		size_t endPos = matchPos + charMatches.length();

		addWhiteSpaceWarning(matchPos, endPos);
		// temporary fix
		reference->pattern.replacePattern(matchPos, endPos, " ");
		lastIndex = matchPos + " "sv.size();
	}

	if (reference->pattern.text == ""s + argumentChar) {
		// this pattern has already been deduced.
	} else {
		// add pattern references
		addPatternReference(reference);
	}
	return true;
}

void Section::addVariableReference(ParseContext &context, VariableReference *reference) {
	variableReferences[reference->name].push_back(reference);
	searchParentPatterns(context, reference);
}

void Section::searchParentPatterns(ParseContext &context, VariableReference *reference) {
	bool found = false;
	// check if this variable name exists in our patterns
	for (PatternDefinition *definition : patternDefinitions) {
		for (PatternElement &element : definition->patternElements) {
			if (element.type != PatternElement::Type::Other && element.text == reference->name) {
				if (element.type != PatternElement::Type::Variable) {
					element.type = PatternElement::Type::Variable;
					if (!found) {
						// add a variable definition for this pattern element
						variableDefinitions[element.text] = new VariableReference(
							Range(
								definition->range.line, definition->range.start() + element.startPos,
								definition->range.start() + element.startPos + element.text.length()
							),
							element.text
						);
					}
				}
				if (!found)
					reference->definition = variableDefinitions[element.text];
				found = true;
			}
		}
	}
	if (!found) {
		// propagate to parent
		if (parent) {
			parent->searchParentPatterns(context, reference);
		} else {
			// no pattern element found, add to unresolved
			context.unresolvedVariableReferences[reference->name].push_back(reference);
		}
	}
}

void Section::addPatternReference(PatternReference *reference) {
	patternReferences.push_back(reference);
	incrementUnresolved();
}

void Section::incrementUnresolved() {
	if (unresolvedCount == 0 && parent) {
		parent->incrementUnresolved();
	}
	unresolvedCount++;
}

void Section::decrementUnresolved() {
	unresolvedCount--;
	if (unresolvedCount == 0 && parent) {
		parent->decrementUnresolved();
	}
}
