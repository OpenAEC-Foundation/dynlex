#include "section.h"
#include "effectSection.h"
#include "expression.h"
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
	line->expression = detectPatterns(context, Range(line, line->patternText), SectionType::Effect);
	return line->expression != nullptr;
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
		line->expression = detectPatterns(context, Range(line, line->patternText), SectionType::Section);
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

Expression *Section::detectPatterns(ParseContext &context, Range range, SectionType patternType) {
	StringHierarchy *hierarchy = createHierarchy(context, range);
	if (!hierarchy)
		return nullptr;
	Expression *expr = detectPatternsRecursively(context, range, hierarchy, patternType);
	delete hierarchy;
	return expr;
}

Expression *
Section::detectPatternsRecursively(ParseContext &context, Range range, StringHierarchy *node, SectionType patternType) {
	Range relativeRange = Range(range.line, range.subString.substr(node->start, node->end - node->start));

	Expression *expr = new Expression();
	expr->range = relativeRange;

	// Check if this is a string literal
	if (relativeRange.subString.starts_with("\"") && relativeRange.subString.ends_with("\"")) {
		expr->kind = Expression::Kind::Literal;
		expr->literalValue = std::string(relativeRange.subString.substr(1, relativeRange.subString.size() - 2));
		return expr;
	}

	// Check if this is a number literal
	std::regex numberRegex("^\\d+(?:\\.\\d+)?$");
	std::string rangeStr(relativeRange.subString);
	if (std::regex_match(rangeStr, numberRegex)) {
		expr->kind = Expression::Kind::Literal;
		if (rangeStr.find('.') != std::string::npos) {
			expr->literalValue = std::stod(rangeStr);
		} else {
			expr->literalValue = static_cast<int64_t>(std::stoll(rangeStr));
		}
		return expr;
	}

	// This is a pending pattern reference (will be resolved later)
	expr->kind = Expression::Kind::Pending;

	// Create a PatternReference for pattern matching
	PatternReference *reference = new PatternReference(relativeRange, patternType);
	expr->patternReference = reference;

	// Process children to find arguments
	auto delegate = [this, &context, &range, &expr](StringHierarchy *childNode) -> bool {
		Expression *childExpr = detectPatternsRecursively(
			context, range.subRange(childNode->start, childNode->end), childNode->cloneWithOffset(-childNode->start),
			SectionType::Expression
		);
		if (!childExpr)
			return false;
		expr->arguments.push_back(childExpr);
		return true;
	};

	constexpr std::string_view intrinsicKeyword = "@intrinsic"sv;
	for (StringHierarchy *child : node->children) {
		if (child->charachter == '(') {
			size_t parenPos = child->start - 1; // position of '(' in relativeRange

			// Check if @intrinsic precedes this parenthesis
			if (parenPos >= intrinsicKeyword.length() &&
				relativeRange.subString.substr(parenPos - intrinsicKeyword.length(), intrinsicKeyword.length()) ==
					intrinsicKeyword) {
				// This is an @intrinsic(...) call
				size_t intrinsicStart = parenPos - intrinsicKeyword.length();
				size_t intrinsicEnd = child->end + 1; // +1 for closing ')'

				Expression *intrinsicExpr = new Expression();
				intrinsicExpr->range = range.subRange(intrinsicStart, intrinsicEnd);
				intrinsicExpr->kind = Expression::Kind::IntrinsicCall;

				// Process arguments - first argument is the intrinsic name
				auto processIntrinsicArg = [&](StringHierarchy *argNode) -> bool {
					Expression *argExpr = detectPatternsRecursively(
						context, range.subRange(argNode->start, argNode->end), argNode->cloneWithOffset(-argNode->start),
						SectionType::Expression
					);
					if (!argExpr)
						return false;

					// First string argument becomes the intrinsic name
					if (intrinsicExpr->intrinsicName.empty() && argExpr->kind == Expression::Kind::Literal) {
						if (auto *str = std::get_if<std::string>(&argExpr->literalValue)) {
							intrinsicExpr->intrinsicName = *str;
							delete argExpr;
							return true;
						}
					}
					intrinsicExpr->arguments.push_back(argExpr);
					return true;
				};

				if (child->children.size() && child->children[0]->charachter == ',') {
					for (StringHierarchy *subChild : child->children) {
						if (!processIntrinsicArg(subChild))
							return nullptr;
					}
				} else if (child->children.size()) {
					if (!processIntrinsicArg(child->children[0]))
						return nullptr;
				}

				expr->arguments.push_back(intrinsicExpr);
				reference->pattern.replaceLine(intrinsicStart, intrinsicEnd);
			} else {
				// Regular parentheses - process arguments inside
				if (child->children.size() && child->children[0]->charachter == ',') {
					for (StringHierarchy *subChild : child->children) {
						if (!delegate(subChild))
							return nullptr;
					}
				} else {
					if (!delegate(child))
						return nullptr;
				}
				reference->pattern.replaceLine(child->start - "("sv.length(), child->end + ")"sv.length());
			}
		} else if (child->charachter == '"') {
			reference->pattern.replaceLine(child->start - "\""sv.length(), child->end + "\""sv.length());
		}
	}

	// Replace number literals in pattern text
	std::regex numLiteralRegex("\\d+(?:\\.\\d+)?");
	std::cregex_iterator iter(range.subString.begin(), range.subString.end(), numLiteralRegex);
	std::cregex_iterator end;
	for (; iter != end; ++iter) {
		reference->pattern.replaceLine(iter->position(), iter->position() + iter->length());
	}

	// Whitespace handling
	auto addWhiteSpaceWarning = [&context, &range, &reference](size_t start, size_t end) {
		context.diagnostics.push_back(Diagnostic(
			Diagnostic::Level::Warning, "all whitespace in patterns should be a single space",
			range.subRange(reference->pattern.getLinePos(start), reference->pattern.getLinePos(end))
		));
	};

	std::smatch matches;

	// Trim left
	std::regex_search(reference->pattern.text, matches, std::regex("^(\\s*)"));
	std::string leftWhiteSpace = matches[0];
	if (!leftWhiteSpace.empty()) {
		if (leftWhiteSpace != " ") {
			addWhiteSpaceWarning(0, leftWhiteSpace.size());
		}
		reference->pattern.replacePattern(0, leftWhiteSpace.size(), "");
	}

	// Trim right
	std::regex_search(reference->pattern.text, matches, std::regex("(\\s*)$"));
	std::string rightWhiteSpace = matches[0];
	if (!rightWhiteSpace.empty()) {
		if (rightWhiteSpace != " ") {
			addWhiteSpaceWarning(matches.position(), reference->pattern.text.size());
		}
		reference->pattern.replacePattern(0, rightWhiteSpace.size(), "");
	}

	// Normalize whitespace
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
		reference->pattern.replacePattern(matchPos, endPos, " ");
		lastIndex = matchPos + " "sv.size();
	}

	// Only add pattern reference if it's not just an argument placeholder
	if (reference->pattern.text != ""s + argumentChar) {
		addPatternReference(reference);
	}

	return expr;
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
