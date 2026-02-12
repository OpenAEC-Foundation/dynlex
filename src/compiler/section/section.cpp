#include "section.h"
#include "classSection.h"
#include "effectSection.h"
#include "expression.h"
#include "expressionSection.h"
#include "parseContext.h"
#include "patternTreeNode.h"
#include "sectionSection.h"
#include "stringHierarchy.h"
#include <stack>
using namespace std::literals;

// Process escape sequences in a string literal
static std::string processEscapeSequences(std::string_view input) {
	static const std::unordered_map<char, char> escapes = {{'n', '\n'}, {'t', '\t'}, {'r', '\r'},  {'a', '\a'}, {'b', '\b'},
														   {'f', '\f'}, {'v', '\v'}, {'\\', '\\'}, {'"', '"'},	{'0', '\0'}};
	std::string result;
	result.reserve(input.size());
	for (size_t i = 0; i < input.size(); ++i) {
		if (input[i] == '\\' && i + 1 < input.size()) {
			auto it = escapes.find(input[++i]);
			result += (it != escapes.end()) ? it->second : input[i];
		} else {
			result += input[i];
		}
	}
	return result;
}

void Section::collectPatternReferencesAndSections(
	std::list<PatternReference *> &bodyReferences, std::list<PatternReference *> &globalReferences,
	std::list<Section *> &sections, bool insideDefinition
) {
	auto &targetList = insideDefinition ? bodyReferences : globalReferences;
	targetList.insert(targetList.end(), patternReferences.begin(), patternReferences.end());
	if (!patternDefinitions.empty())
		sections.push_back(this);
	bool childInsideDefinition = insideDefinition || !patternDefinitions.empty();
	for (Section *child : children) {
		child->collectPatternReferencesAndSections(bodyReferences, globalReferences, sections, childInsideDefinition);
	}
}

bool Section::processLine(ParseContext &context, CodeLine *line) {
	line->expression = detectPatterns(context, Range(line, line->patternText), SectionType::Effect);
	return line->expression != nullptr;
}

Section *Section::createSection(ParseContext &context, CodeLine *line) {
	// determine the section type by parsing keywords
	std::string_view remaining = line->patternText;
	Section *newSection{};
	bool isMacro = false;
	bool isLocal = false;

	// Parse keywords until we hit a section type keyword (effect, expression)
	while (!remaining.empty()) {
		std::size_t spaceIndex = remaining.find(' ');
		std::string_view current = (spaceIndex != std::string::npos) ? remaining.substr(0, spaceIndex) : remaining;
		remaining = (spaceIndex != std::string::npos) ? remaining.substr(spaceIndex + 1) : std::string_view{};

		if (current == "macro") {
			isMacro = true;
		} else if (current == "local") {
			isLocal = true;
		} else if (current == "effect") {
			newSection = new EffectSection(this);
			break;
		} else if (current == "expression") {
			newSection = new ExpressionSection(this);
			break;
		} else if (current == "section") {
			newSection = new SectionSection(this);
			break;
		} else if (current == "class") {
			newSection = new ClassSection(this);
			break;
		} else {
			// Unknown keyword - not a section definition
			break;
		}
	}

	if (newSection) {
		newSection->isMacro = isMacro;
		newSection->isLocal = isLocal;
		// Remaining contains the pattern after the section type keyword
		if (!remaining.empty()) {
			newSection->patternDefinitions.push_back(new PatternDefinition(Range(line, remaining), newSection));
		}
	}
	if (!newSection) {
		// custom section
		newSection = new Section(SectionType::Custom, this);
		// detectPatterns already adds the pattern reference via detectPatternsRecursively
		line->expression = detectPatterns(context, Range(line, line->patternText), SectionType::Section);
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

static Expression *createStringLiteral(Range range, StringHierarchy *strNode) {
	Expression *strExpr = new Expression();
	strExpr->range = range.subRange(strNode->start - 1, strNode->end + 1);
	strExpr->kind = Expression::Kind::Literal;
	strExpr->literalValue = processEscapeSequences(range.subString.substr(strNode->start, strNode->end - strNode->start));
	return strExpr;
}

Expression *
Section::detectPatternsRecursively(ParseContext &context, Range range, StringHierarchy *node, SectionType patternType) {
	Range relativeRange = Range(range.line, range.subString.substr(node->start, node->end - node->start));

	Expression *expr = new Expression();
	expr->range = relativeRange;
	// This is a pending pattern reference (will be resolved later)
	expr->kind = Expression::Kind::Pending;

	// Create a PatternReference for pattern matching
	PatternReference *reference = new PatternReference(expr, patternType);
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
					Expression *argExpr;
					if (argNode->charachter == '"') {
						argExpr = createStringLiteral(range, argNode);
					} else {
						argExpr = detectPatternsRecursively(
							context, range.subRange(argNode->start, argNode->end), argNode->cloneWithOffset(-argNode->start),
							SectionType::Expression
						);
					}
					if (!argExpr)
						return false;

					// First string argument becomes the intrinsic name
					if (intrinsicExpr->intrinsicName.empty() && argExpr->kind == Expression::Kind::Literal) {
						if (auto *str = std::get_if<std::string>(&argExpr->literalValue)) {
							intrinsicExpr->intrinsicName = *str;
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
			expr->arguments.push_back(createStringLiteral(range, child));
			reference->pattern.replaceLine(child->start - "\""sv.length(), child->end + "\""sv.length());
		}
	}

	// Replace number literals in pattern text and create sub-expressions.
	// Search the transformed pattern text (where strings/intrinsics are already replaced with \a)
	// to avoid matching digits inside string literals (e.g. "i64").
	std::regex numLiteralRegex("\\b\\d+(?:\\.\\d+)?\\b");
	std::string patternSnapshot = reference->pattern.text;
	std::sregex_iterator iter(patternSnapshot.begin(), patternSnapshot.end(), numLiteralRegex);
	std::sregex_iterator end;
	// Collect matches, then process in reverse so pattern positions stay valid
	std::vector<std::tuple<size_t, size_t, std::string>> numMatches;
	for (; iter != end; ++iter)
		numMatches.emplace_back(iter->position(), iter->position() + iter->length(), iter->str());
	for (auto it = numMatches.rbegin(); it != numMatches.rend(); ++it) {
		auto &[pos, endPos, numStr] = *it;
		Expression *numExpr = new Expression();
		size_t lineStart = reference->pattern.getLinePos(pos);
		size_t lineEnd = reference->pattern.getLinePos(endPos);
		numExpr->range = relativeRange.subRange(lineStart, lineEnd);
		numExpr->kind = Expression::Kind::Literal;
		if (numStr.find('.') != std::string::npos) {
			numExpr->literalValue = std::stod(numStr);
		} else {
			numExpr->literalValue = static_cast<int64_t>(std::stoll(numStr));
		}
		expr->arguments.push_back(numExpr);
		reference->pattern.replacePattern(pos, endPos);
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
		reference->pattern.replacePattern(matches.position(), reference->pattern.text.size(), "");
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

	// If pattern is just an argument placeholder, return the argument directly
	// This happens for expressions or for intrinsic calls (which are effects on their own)
	if (reference->pattern.text == ""s + argumentChar) {
		Expression *arg = expr->arguments[0];
		if (patternType == SectionType::Expression || arg->kind == Expression::Kind::IntrinsicCall) {
			delete expr;
			delete reference;
			return arg;
		}
	}

	addPatternReference(reference);
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
		forEachLeafElement(definition->patternElements, [&](PatternElement &element) {
			if (element.text != reference->name)
				return;
			auto markFound = [&] {
				if (!found) {
					VariableReference *varRef = new VariableReference(
						Range(
							definition->range.line, definition->range.start() + element.startPos,
							definition->range.start() + element.startPos + element.text.length()
						),
						element.text
					);
					variableDefinitions[element.text] = varRef;
					variableReferences[element.text].push_back(varRef);
					reference->definition = varRef;
				}
				found = true;
			};
			if (element.type == PatternElement::Type::Variable || element.type == PatternElement::Type::VariableLike) {
				if (element.type != PatternElement::Type::Variable)
					element.type = PatternElement::Type::Variable;
				markFound();
			} else if (element.type == PatternElement::Type::Word) {
				// Word captures match by name but stay as Word — the parameter
				// is bound to a string literal at call time via parameterNames on the tree node
				markFound();
			}
		});
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

Variable *Section::findVariable(const std::string &name) {
	Section *sec = this;
	while (sec) {
		auto it = sec->variables.find(name);
		if (it != sec->variables.end())
			return it->second;
		sec = sec->parent;
	}
	return nullptr;
}