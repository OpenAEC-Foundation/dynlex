#include "section.h"
#include "bindingResolution.h"
#include "classSection.h"
#include "expression.h"
#include "functionSection.h"
#include "intrinsicInfo.h"
#include "numericLiteral.h"
#include "parseContext.h"
#include "patternTreeNode.h"
#include "sectionSection.h"
#include "stringHierarchy.h"
#include "syntaxConfig.h"
#include "variable.h"
#include <cctype>
#include <iostream>
#include <regex>
#include <stack>
using namespace std::literals;

Expression *&InstantiatedSectionBody::lineExpression(size_t index) {
	requireCompilerInvariant(index < lineExpressions.size(), "instantiated body line index is out of range");
	return lineExpressions[index];
}

InstantiatedSectionBody *InstantiatedSectionBody::bodyForChild(Section *child) const {
	for (const auto &body : childBodies) {
		if (body && body->sourceSection == child)
			return body.get();
	}
	return nullptr;
}

Expression *InstantiatedSectionBody::findCloneOf(const Expression *templateExpression) const {
	Expression *result = nullptr;
	for (Expression *root : lineExpressions) {
		visitExpressionTree(root, [&](Expression *expression) {
			if (expression == templateExpression || expression->reusableTemplateExpression == templateExpression) {
				result = expression;
				return true;
			}
			return false;
		});
		if (result)
			return result;
	}
	for (const auto &child : childBodies) {
		if (child) {
			result = child->findCloneOf(templateExpression);
			if (result)
				return result;
		}
	}
	return nullptr;
}

std::optional<CompileTimeValue>
InstantiatedSectionBody::compileTimeValueForReference(const VariableReference *reference) const {
	if (!reference)
		return std::nullopt;
	for (Expression *root : lineExpressions) {
		std::optional<CompileTimeValue> result;
		visitExpressionTree(root, [&](Expression *expression) {
			if (expression->kind != Expression::Kind::Variable || expression->variable != reference ||
				std::holds_alternative<std::monostate>(expression->compileTimeValue))
				return false;
			result = expression->compileTimeValue;
			return true;
		});
		if (result)
			return result;
	}
	for (const auto &child : childBodies) {
		if (!child)
			continue;
		std::optional<CompileTimeValue> result = child->compileTimeValueForReference(reference);
		if (result)
			return result;
	}
	return std::nullopt;
}

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
	line->expression = detectPatterns(context, Range(line, line->patternText), SectionType::Function);
	return line->expression != nullptr;
}

Section *Section::createSection(ParseContext &context, CodeLine *line) {
	// determine the section type by parsing keywords
	const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
	std::string_view remaining = line->patternText;
	Section *newSection{};
	bool isFlex = false;
	bool isLocal = false;
	bool isExposed = false;
	bool isImplicit = false;

	if (line->definitionShorthand != DefinitionShorthand::None) {
		newSection = new FunctionSection(this);
		switch (line->definitionShorthand) {
		case DefinitionShorthand::Action:
			newSection->returnContract = DefinitionReturnContract::Nothing;
			break;
		case DefinitionShorthand::Value:
			newSection->returnContract = DefinitionReturnContract::Value;
			break;
		case DefinitionShorthand::Replacement:
			newSection->returnContract = DefinitionReturnContract::ReplacementValue;
			isFlex = true;
			break;
		case DefinitionShorthand::None:
			crashCompilerBug("missing definition shorthand kind");
		}
	} else {
		// Parse keywords until we hit a section type keyword (function, section)
		while (!remaining.empty()) {
			std::size_t spaceIndex = remaining.find(' ');
			std::string_view current = (spaceIndex != std::string::npos) ? remaining.substr(0, spaceIndex) : remaining;
			remaining = (spaceIndex != std::string::npos) ? remaining.substr(spaceIndex + 1) : std::string_view{};

			if (current == syntax.flexName) {
				isFlex = true;
			} else if (current == syntax.localName) {
				isLocal = true;
			} else if (current == syntax.exposedName) {
				isExposed = true;
			} else if (current == syntax.implicitName) {
				isImplicit = true;
			} else if (current == syntax.functionName) {
				newSection = new FunctionSection(this);
				break;
			} else if (current == syntax.conversionName) {
				newSection = new FunctionSection(this);
				newSection->isConversion = true;
				newSection->isImplicitConversion = isImplicit;
				break;
			} else if (current == syntax.sectionName) {
				newSection = new SectionSection(this);
				break;
			} else if (current == syntax.className) {
				newSection = new ClassSection(this);
				break;
			} else {
				// Unknown keyword - not a section definition
				break;
			}
		}
	}

	if (newSection) {
		newSection->isFlex = isFlex;
		newSection->isLocal = isLocal;
		newSection->isExposed = isExposed;
		if (isImplicit && !newSection->isConversion) {
			context.addDiagnostic(Diagnostic(
				context, Diagnostic::Level::Error, "implicit modifier requires conversion", Range(line, line->patternText),
				"modifier", syntax.implicitName, "conversion", syntax.conversionName
			));
		}
		// Remaining contains the pattern after the section type keyword
		if (!remaining.empty()) {
			newSection->patternDefinitions.push_back(new PatternDefinition(Range(line, remaining), newSection));
		} else if (newSection->isConversion) {
			context.addDiagnostic(Diagnostic(
				context, Diagnostic::Level::Error, "conversion requires one parameter", Range(line, line->patternText)
			));
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

bool Section::finalize(ParseContext & /*context*/) { return true; }

StringHierarchy *parseBracketHierarchy(ParseContext &context, Range range) {
	std::stack<StringHierarchy *> nodeStack;
	StringHierarchy *base = new StringHierarchy(0, 0);
	nodeStack.push(base);

	for (size_t index = 0; index < range.subString.size(); index++) {
		char character = range.subString[index];
		if (character == argumentChar)
			crashCompilerBug("internal argument placeholder reached bracket parsing");

		auto push = [&nodeStack, index, character] {
			StringHierarchy *newChild = new StringHierarchy(character, index + 1);
			nodeStack.top()->children.push_back(newChild);
			nodeStack.push(newChild);
		};
		auto tryPop = [&nodeStack, &context, &range, base, index, character](char requiredCharacter) {
			if (nodeStack.top()->character == requiredCharacter) {
				nodeStack.top()->end = index;
				nodeStack.pop();
				return true;
			} else {
				delete base;
				context.diagnostics.push_back(Diagnostic(
					context, Diagnostic::Level::Error, "unmatched closing character",
					Range(range.line, range.subString.substr(index, 1)), "character", std::string(1, character)
				));
				return false;
			}
		};

		switch (character) {
		case '(': {
			push();
			break;
		}
		case '[': {
			push();
			break;
		}
		case '{': {
			push();
			break;
		}
		case ')': {
			if (nodeStack.top()->character == ',') {
				nodeStack.top()->end = index;
				nodeStack.pop();
			}
			if (!tryPop('('))
				return nullptr;
			break;
		}
		case ']': {
			if (nodeStack.top()->character == ',') {
				nodeStack.top()->end = index;
				nodeStack.pop();
			}
			if (!tryPop('['))
				return nullptr;
			break;
		}
		case '}': {
			if (!tryPop('{'))
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
						context, Diagnostic::Level::Error, "unmatched string character",
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
			if (nodeStack.top()->character == '"')
				// skip the next character
				index++;
			break;
		}
		case ',': {
			// Commas separate arguments only in @intrinsic argument lists and
			// array literals. In grouping parentheses they stay ordinary text,
			// so patterns containing a literal comma (like the select pattern
			// "$ if $, else $") still match inside them.
			auto isIntrinsicArgumentParen = [&range](const StringHierarchy *parenNode) {
				constexpr std::string_view intrinsicKeyword = "@intrinsic"sv;
				if (parenNode->start < 1)
					return false;
				size_t parenPos = static_cast<size_t>(parenNode->start) - 1;
				return parenPos >= intrinsicKeyword.length() &&
					   range.subString.substr(parenPos - intrinsicKeyword.length(), intrinsicKeyword.length()) ==
						   intrinsicKeyword;
			};
			if (nodeStack.top()->character == '[' ||
				(nodeStack.top()->character == '(' && isIntrinsicArgumentParen(nodeStack.top()))) {
				// add the child, don't push
				StringHierarchy *newChild = new StringHierarchy(character, nodeStack.top()->start);
				// move all other children to this new child
				newChild->children = nodeStack.top()->children;
				newChild->end = index;
				nodeStack.top()->children = {newChild};
				// add another ',' child and push
				push();
			} else if (nodeStack.top()->character == ',') {
				nodeStack.top()->end = index;
				nodeStack.pop();
				push();
			} else {
				// Grouping-paren or top-level comma — treat as regular text
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
				context, Diagnostic::Level::Error, "unmatched closing character",
				range.subRange(nodeStack.top()->start, nodeStack.top()->start + 1), "character",
				std::string(1, nodeStack.top()->character)
			));
			nodeStack.pop();
		}
		delete base;
		return nullptr;
	}

	base->end = range.end();
	return base;
}

Expression *
Section::detectPatterns(ParseContext &context, Range range, SectionType patternType, bool registerPatternReferences) {
	StringHierarchy *hierarchy = parseBracketHierarchy(context, range);
	if (!hierarchy)
		return nullptr;
	Expression *expr = detectPatternsRecursively(context, range, hierarchy, patternType, registerPatternReferences);
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

static Expression *createArrayLiteral(
	Section *section, ParseContext &context, Range range, StringHierarchy *arrayNode, bool registerPatternReferences
) {
	Expression *arrayExpr = new Expression();
	arrayExpr->range = range.subRange(arrayNode->start - 1, arrayNode->end + 1);
	arrayExpr->kind = Expression::Kind::ArrayLiteral;

	auto processElement = [&](StringHierarchy *elementNode) -> bool {
		Expression *elementExpr = nullptr;
		if (elementNode->character == '"') {
			elementExpr = createStringLiteral(range, elementNode);
		} else {
			StringHierarchy *clonedNode = elementNode->cloneWithOffset(-elementNode->start);
			elementExpr = section->detectPatternsRecursively(
				context, range.subRange(elementNode->start, elementNode->end), clonedNode, SectionType::Function,
				registerPatternReferences
			);
			delete clonedNode;
		}
		if (!elementExpr)
			return false;
		elementExpr->isExplicitGroup = true;
		arrayExpr->arguments.push_back(elementExpr);
		return true;
	};

	if (!arrayNode->children.empty()) {
		if (arrayNode->children[0]->character == ',') {
			for (StringHierarchy *child : arrayNode->children) {
				if (!processElement(child))
					return nullptr;
			}
		} else {
			if (!processElement(arrayNode->children[0]))
				return nullptr;
		}
	} else {
		size_t elementStart = arrayNode->start;
		size_t elementEnd = arrayNode->end;
		while (elementStart < elementEnd && std::isspace(static_cast<unsigned char>(range.subString[elementStart])))
			elementStart++;
		while (elementEnd > elementStart && std::isspace(static_cast<unsigned char>(range.subString[elementEnd - 1])))
			elementEnd--;
		if (elementStart < elementEnd) {
			StringHierarchy elementNode(0, elementStart);
			elementNode.end = elementEnd;
			if (!processElement(&elementNode))
				return nullptr;
		}
	}
	return arrayExpr;
}

Expression *Section::detectPatternsRecursively(
	ParseContext &context, Range range, StringHierarchy *node, SectionType patternType, bool registerPatternReferences
) {
	Range relativeRange = Range(range.line, range.subString.substr(node->start, node->end - node->start));

	Expression *expr = new Expression();
	expr->range = relativeRange;
	// This is a pending pattern reference (will be resolved later)
	expr->kind = Expression::Kind::Pending;

	// Create a PatternReference for pattern matching
	PatternReference *reference = new PatternReference(expr, patternType);
	reference->matchingScope = this;
	expr->patternReference = reference;

	// Process children to find arguments
	auto delegate = [this, &context, &range, &expr, registerPatternReferences](StringHierarchy *childNode) -> bool {
		StringHierarchy *clonedNode = childNode->cloneWithOffset(-childNode->start);
		Expression *childExpr = detectPatternsRecursively(
			context, range.subRange(childNode->start, childNode->end), clonedNode, SectionType::Function,
			registerPatternReferences
		);
		delete clonedNode;
		if (!childExpr)
			return false;
		childExpr->isExplicitGroup = true;
		expr->arguments.push_back(childExpr);
		return true;
	};

	constexpr std::string_view intrinsicKeyword = "@intrinsic"sv;
	for (StringHierarchy *child : node->children) {
		if (child->character == '(') {
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
					if (argNode->character == '"') {
						argExpr = createStringLiteral(range, argNode);
					} else {
						StringHierarchy *clonedNode = argNode->cloneWithOffset(-argNode->start);
						argExpr = detectPatternsRecursively(
							context, range.subRange(argNode->start, argNode->end), clonedNode, SectionType::Function,
							registerPatternReferences
						);
						delete clonedNode;
					}
					if (!argExpr)
						return false;
					if (argNode->character == '(')
						argExpr->isExplicitGroup = true;

					// First string argument becomes the intrinsic name
					if (intrinsicExpr->intrinsicName.empty() && argExpr->kind == Expression::Kind::Literal) {
						if (auto *str = std::get_if<std::string>(&argExpr->literalValue)) {
							intrinsicExpr->intrinsicName = *str;
						}
					}
					intrinsicExpr->arguments.push_back(argExpr);
					return true;
				};

				if (child->children.size() && child->children[0]->character == ',') {
					for (StringHierarchy *subChild : child->children) {
						if (!processIntrinsicArg(subChild))
							return nullptr;
					}
				} else if (child->children.size()) {
					if (!processIntrinsicArg(child->children[0]))
						return nullptr;
				}

				// Validate argument count against intrinsic registry
				if (!intrinsicExpr->intrinsicName.empty()) {
					const IntrinsicInfo *info = findIntrinsic(intrinsicExpr->intrinsicName);
					if (!info) {
						context.diagnostics.push_back(Diagnostic(
							context, Diagnostic::Level::Error, "unknown intrinsic", intrinsicExpr->range, "intrinsic",
							intrinsicExpr->intrinsicName
						));
						return nullptr;
					}
					int argCount = (int)intrinsicExpr->arguments.size();
					bool belowMin = argCount < info->minArgCount;
					bool aboveMax = info->maxArgCount >= 0 && argCount > info->maxArgCount;
					if (belowMin || aboveMax) {
						std::string expected;
						if (info->maxArgCount < 0)
							expected = "at least " + std::to_string(info->minArgCount - 1);
						else if (info->minArgCount == info->maxArgCount)
							expected = std::to_string(info->minArgCount - 1);
						else
							expected = std::to_string(info->minArgCount - 1) + " to " + std::to_string(info->maxArgCount - 1);
						context.diagnostics.push_back(Diagnostic(
							context, Diagnostic::Level::Error, "intrinsic wrong argument count", intrinsicExpr->range,
							"intrinsic", intrinsicExpr->intrinsicName, "expected", expected, "found",
							std::to_string(argCount - 1)
						));
						return nullptr;
					}
				}

				context.processEncounteredIntrinsic(intrinsicExpr);
				expr->arguments.push_back(intrinsicExpr);
				reference->pattern.replaceLine(intrinsicStart, intrinsicEnd);
			} else {
				// Regular grouping parentheses hold exactly one sub-expression;
				// commas only separate arguments in @intrinsic lists and arrays.
				if (!delegate(child))
					return nullptr;
				reference->pattern.replaceLine(child->start - "("sv.length(), child->end + ")"sv.length());
			}
		} else if (child->character == '[') {
			Expression *arrayExpr = createArrayLiteral(this, context, range, child, registerPatternReferences);
			if (!arrayExpr)
				return nullptr;
			expr->arguments.push_back(arrayExpr);
			reference->pattern.replaceLine(child->start - "["sv.length(), child->end + "]"sv.length());
		} else if (child->character == '"') {
			expr->arguments.push_back(createStringLiteral(range, child));
			reference->pattern.replaceLine(child->start - "\""sv.length(), child->end + "\""sv.length());
		} else if (child->character == '{') {
			std::string_view content = range.subString.substr(child->start, child->end - child->start);
			CaptureElementParts capture = splitCaptureElement(content);
			if (capture.name.empty() || !isValidVariableName(capture.name) ||
				(content.find(':') != std::string_view::npos && capture.typeConstraint.empty())) {
				context.addDiagnostic(Diagnostic(
					context, Diagnostic::Level::Error, "invalid pattern parse capture format",
					range.subRange(child->start - 1, child->start)
				));
				delete reference;
				delete expr;
				return nullptr;
			}

			Range nameRange =
				range.subRange(child->start + capture.nameOffset, child->start + capture.nameOffset + capture.name.size());
			VariableReference *variableReference = context.createVariableReference(nameRange, std::string(capture.name));
			if (!capture.typeConstraint.empty()) {
				variableReference->declaredTypeConstraintName = std::string(capture.typeConstraint);
				variableReference->declaredTypeConstraintRange =
					range.subRange(child->start, child->start + capture.typeConstraint.size());
			}
			if (registerPatternReferences) {
				if (variableReference->declaredTypeConstraintName.empty())
					registerExplicitVariableName(variableReference->name, variableReference->range);
				context.pendingExplicitVariableReferences.push_back(variableReference);
			} else {
				auto definition = variableDefinitions.find(variableReference->name);
				if (definition != variableDefinitions.end())
					variableReference->definition = normalizeBindingReference(definition->second);
			}

			Expression *variableExpression = new Expression();
			variableExpression->range = range.subRange(child->start - 1, child->end + 1);
			variableExpression->kind = Expression::Kind::Variable;
			variableExpression->variable = variableReference;
			expr->arguments.push_back(variableExpression);
			context.addSourceToken(nameRange, ParseContext::SourceTokenKind::Variable);
			reference->pattern.replaceLine(child->start - 1, child->end + 1);
		}
	}

	// Replace number literals in pattern text and create sub-expressions.
	// Search the transformed pattern text (where strings/intrinsics are already replaced with \a)
	// to avoid matching digits inside string literals (e.g. "i64").
	std::string patternSnapshot = reference->pattern.text;
	// Collect matches, then process in reverse so pattern positions stay valid.
	// Number functions are collected separately and added in forward (left-to-right) order
	// after all pattern replacements, so that sourceArgumentIndex maps to the correct function.
	std::vector<std::tuple<size_t, size_t, std::string>> numMatches;

	for (size_t pos = 0; pos < patternSnapshot.size();) {
		// Number literals are intentionally unsigned.
		// Unary minus is modeled as a real operator pattern (e.g. "-value"),
		// not as part of lexical number parsing.
		size_t start = pos;
		if (pos > 0) {
			unsigned char prev = static_cast<unsigned char>(patternSnapshot[pos - 1]);
			if (std::isalnum(prev) || prev == '_') {
				pos = start + 1;
				continue;
			}
		}

		if (pos >= patternSnapshot.size() || !std::isdigit(static_cast<unsigned char>(patternSnapshot[pos]))) {
			pos = start + 1;
			continue;
		}

		size_t intStart = pos;
		while (pos < patternSnapshot.size() && std::isdigit(static_cast<unsigned char>(patternSnapshot[pos])))
			pos++;
		if (pos < patternSnapshot.size() && patternSnapshot[pos] == '.') {
			size_t dotPos = pos;
			pos++;
			size_t fracStart = pos;
			while (pos < patternSnapshot.size() && std::isdigit(static_cast<unsigned char>(patternSnapshot[pos])))
				pos++;
			if (fracStart == pos)
				pos = dotPos; // keep integer-only match if '.' isn't followed by digits
		}

		// Word boundary on the right to avoid partial matches in identifiers.
		if (pos < patternSnapshot.size() && std::isalnum(static_cast<unsigned char>(patternSnapshot[pos]))) {
			pos = intStart + 1;
			continue;
		}

		numMatches.emplace_back(intStart, pos, std::string(patternSnapshot.substr(intStart, pos - intStart)));
	}
	std::vector<Expression *> numExprs;
	std::vector<NumericLiteralValue> numericValues;
	numericValues.reserve(numMatches.size());
	for (const auto &[pos, endPos, numStr] : numMatches) {
		NumericLiteralParseResult parsed = parseNumericLiteral(numStr);
		if (!parsed) {
			Range literalRange =
				relativeRange.subRange(reference->pattern.getLinePos(pos), reference->pattern.getLinePos(endPos));
			std::string_view diagnosticKey;
			switch (parsed.error) {
			case NumericLiteralParseError::IntegerOutOfRange:
				diagnosticKey = "integer literal out of range";
				break;
			case NumericLiteralParseError::FloatingPointOutOfRange:
				diagnosticKey = "floating point literal out of range";
				break;
			case NumericLiteralParseError::Invalid:
				diagnosticKey = "invalid numeric literal";
				break;
			case NumericLiteralParseError::None:
				crashCompilerBug("successful numeric literal parse reported failure");
			}
			context.diagnostics.push_back(Diagnostic(context, Diagnostic::Level::Error, diagnosticKey, literalRange));
			return nullptr;
		}
		numericValues.push_back(parsed.value);
	}
	for (size_t reverseIndex = numMatches.size(); reverseIndex > 0; reverseIndex--) {
		auto &[pos, endPos, numStr] = numMatches[reverseIndex - 1];
		(void)numStr;
		Expression *numExpr = new Expression();
		size_t lineStart = reference->pattern.getLinePos(pos);
		size_t lineEnd = reference->pattern.getLinePos(endPos);
		numExpr->range = relativeRange.subRange(lineStart, lineEnd);
		numExpr->kind = Expression::Kind::Literal;
		std::visit([&](auto value) {
			numExpr->literalValue = value;
		}, numericValues[reverseIndex - 1]);
		numExprs.push_back(numExpr);
		reference->pattern.replacePattern(pos, endPos);
	}
	// Reverse to restore left-to-right order (numbers were processed right-to-left above)
	std::reverse(numExprs.begin(), numExprs.end());
	for (Expression *numExpr : numExprs)
		expr->arguments.push_back(numExpr);

	// Sort arguments by source position so sourceArgumentIndex in pattern matching
	// maps correctly (parens and numbers may be interleaved in the text)
	expr->arguments = sortArgumentsByPosition(expr->arguments);

	// Whitespace handling
	auto addWhiteSpaceWarning = [&context, &range, &reference](size_t start, size_t end) {
		context.diagnostics.push_back(Diagnostic(
			context, Diagnostic::Level::Warning, "pattern whitespace should be single space",
			range.subRange(reference->pattern.getLinePos(start), reference->pattern.getLinePos(end))
		));
	};

	static const std::regex leftWhitespaceRegex("^(\\s*)");
	static const std::regex rightWhitespaceRegex("(\\s*)$");
	static const std::regex repeatedOrNonSpaceWhitespaceRegex("\\s{2,}|[^\\S ]");
	std::smatch matches;

	// Trim left
	std::regex_search(reference->pattern.text, matches, leftWhitespaceRegex);
	std::string leftWhiteSpace = matches[0];
	if (!leftWhiteSpace.empty()) {
		if (leftWhiteSpace != " ")
			addWhiteSpaceWarning(0, leftWhiteSpace.size());
		reference->pattern.replacePattern(0, leftWhiteSpace.size(), "");
	}

	// Trim right
	std::regex_search(reference->pattern.text, matches, rightWhitespaceRegex);
	std::string rightWhiteSpace = matches[0];
	if (!rightWhiteSpace.empty()) {
		if (rightWhiteSpace != " ")
			addWhiteSpaceWarning(matches.position(), reference->pattern.text.size());
		reference->pattern.replacePattern(matches.position(), reference->pattern.text.size(), "");
	}

	// Normalize whitespace
	size_t lastIndex = 0;
	std::cmatch charMatches;
	while (std::regex_search(
		reference->pattern.text.c_str() + lastIndex, reference->pattern.text.c_str() + reference->pattern.text.size(),
		charMatches, repeatedOrNonSpaceWhitespaceRegex
	)) {
		size_t matchPos = lastIndex + charMatches.position();
		size_t endPos = matchPos + charMatches.length();
		addWhiteSpaceWarning(matchPos, endPos);
		reference->pattern.replacePattern(matchPos, endPos, " ");
		lastIndex = matchPos + " "sv.size();
	}

	// If pattern is just an argument placeholder, return the argument directly
	// This happens for functions or for intrinsic calls (which are effects on their own)
	if (reference->pattern.text == ""s + argumentChar) {
		if (expr->arguments.empty())
			crashCompilerBug("expression parser generated an argument placeholder without an argument");
		Expression *arg = expr->arguments[0];
		if (patternType == SectionType::Function || arg->kind == Expression::Kind::IntrinsicCall) {
			delete expr;
			delete reference;
			return arg;
		}
	}

	if (registerPatternReferences)
		addPatternReference(reference);
	return expr;
}

void Section::addVariableReference(ParseContext &context, VariableReference *reference) {
	requireCompilerInvariant(reference && reference->range.line, "variable reference has no source position");
	variableReferences[reference->name].push_back(reference);
	auto explicitDeclaration = explicitVariableDeclarations.find(reference->name);
	if (explicitDeclaration != explicitVariableDeclarations.end() &&
		std::pair(explicitDeclaration->second.line->mergedLineIndex, explicitDeclaration->second.start()) <=
			std::pair(reference->range.line->mergedLineIndex, reference->range.start())) {
		context.unresolvedVariableReferences[reference->name].push_back(reference);
		return;
	}
	searchParentPatterns(context, reference);
}

void Section::registerExplicitVariableName(const std::string &name, const Range &declarationRange) {
	requireCompilerInvariant(declarationRange.line, "explicit variable declaration has no source line");
	auto existing = explicitVariableDeclarations.find(name);
	if (existing == explicitVariableDeclarations.end() ||
		std::pair(declarationRange.line->mergedLineIndex, declarationRange.start()) <
			std::pair(existing->second.line->mergedLineIndex, existing->second.start()))
		explicitVariableDeclarations[name] = declarationRange;
}

void Section::indexExplicitParameters(PatternDefinition &definition) {
	requireCompilerInvariant(definition.section == this, "explicit parameter candidate belongs to another section");
	explicitParameterIndex.addDefinition(definition);
	forEachLeafElement(definition.patternElements, [&](const DefinitionPatternElement &element) {
		if (element.type == PatternElement::Type::Variable) {
			int sourceStart = definition.range.start() + static_cast<int>(element.startPos);
			registerExplicitVariableName(
				element.text, Range(definition.range.line, sourceStart, sourceStart + static_cast<int>(element.text.size()))
			);
		}
	});
}

bool Section::canPromoteImplicitParameter(const PatternDefinition &definition, const DefinitionPatternElement &element) const {
	requireCompilerInvariant(definition.section == this, "implicit parameter candidate belongs to another section");
	return canPromoteVariableLikeElement(element) && !explicitParameterIndex.contains(definition, element.text);
}

std::vector<Range> Section::patternParameterCandidateRanges(const std::string &name) const {
	std::vector<Range> ranges;
	std::unordered_set<std::string> seenRanges;
	auto appendRange = [&](const Range &range) {
		if (range.line && seenRanges.insert(range.toString()).second)
			ranges.push_back(range);
	};

	if (const auto *explicitCandidates = explicitParameterIndex.find(name)) {
		for (const ExplicitParameterCandidate &candidate : *explicitCandidates)
			appendRange(candidate.sourceRange);
	}
	for (PatternDefinition *definition : patternDefinitions) {
		if (explicitParameterIndex.contains(*definition, name))
			continue;
		bool found = false;
		forEachLeafElement(definition->patternElements, [&](const DefinitionPatternElement &element) {
			if (found || element.text != name)
				return;
			if (element.type == PatternElement::Type::Variable) {
				if (!element.promotedFromVariableLike)
					return;
			} else if (!canPromoteImplicitParameter(*definition, element)) {
				return;
			}
			int sourceStart = definition->range.start() + static_cast<int>(element.startPos);
			appendRange(Range(definition->range.line, sourceStart, sourceStart + static_cast<int>(element.text.length())));
			found = true;
		});
	}
	return ranges;
}

VariableReference *
Section::resolvePatternParameterBinding(ParseContext &context, const std::string &name, const Range &useRange) {
	auto materializeBinding = [&](const Range &definitionRange) {
		VariableReference *definitionReference = nullptr;
		auto existing = variableDefinitions.find(name);
		if (existing != variableDefinitions.end()) {
			definitionReference = existing->second;
		} else {
			definitionReference = context.createVariableReference(definitionRange, name);
			variableDefinitions[name] = definitionReference;
			variableReferences[name].push_back(definitionReference);
		}
		auto variableIt = variables.find(name);
		if (variableIt == variables.end()) {
			variables[name] = new Variable(name, definitionReference, false);
		} else {
			variableIt->second->definition = definitionReference;
			variableIt->second->name = name;
		}
		return definitionReference;
	};

	const auto *explicitCandidates = explicitParameterIndex.find(name);
	std::unordered_set<PatternDefinition *> definitionsWithExplicitParameter;
	if (explicitCandidates) {
		requireCompilerInvariant(!explicitCandidates->empty(), "explicit parameter index contains an empty candidate list");
		for (const ExplicitParameterCandidate &candidate : *explicitCandidates) {
			requireCompilerInvariant(
				candidate.definition && candidate.definition->section == this,
				"explicit parameter index contains a candidate for another section"
			);
			requireCompilerInvariant(
				candidate.captureType == PatternElement::Type::Variable,
				"explicit parameter index contains a non-capture element"
			);
			definitionsWithExplicitParameter.insert(candidate.definition);
		}
	}

	PatternDefinition *bindingDefinition = nullptr;
	DefinitionPatternElement *bindingElement = nullptr;
	for (PatternDefinition *definition : patternDefinitions) {
		if (definitionsWithExplicitParameter.contains(definition))
			continue;
		visitPatternNameWithFoundState(definition->patternElements, name, false, [&](DefinitionPatternElement &element) {
			if (element.type == PatternElement::Type::Variable) {
				requireCompilerInvariant(
					element.promotedFromVariableLike, "explicit pattern parameter is missing from its candidate index"
				);
			} else {
				if (!canPromoteImplicitParameter(*definition, element))
					return false;
				promoteImplicitPatternParameter(context, *definition, element, useRange);
			}
			if (!bindingElement) {
				bindingDefinition = definition;
				bindingElement = &element;
			}
			return true;
		});
	}
	if (explicitCandidates)
		return materializeBinding(explicitCandidates->front().sourceRange);
	if (!bindingElement)
		return nullptr;

	int sourceStart = bindingDefinition->range.start() + static_cast<int>(bindingElement->startPos);
	return materializeBinding(
		Range(bindingDefinition->range.line, sourceStart, sourceStart + static_cast<int>(bindingElement->text.length()))
	);
}

void Section::searchParentPatterns(ParseContext &context, VariableReference *reference) {
	if (VariableReference *definitionReference = resolvePatternParameterBinding(context, reference->name, reference->range)) {
		reference->definition = definitionReference;
		if (!reference->declaredTypeConstraintName.empty()) {
			auto variable = variables.find(reference->name);
			requireCompilerInvariant(
				variable != variables.end() && variable->second,
				"resolved pattern parameter binding has no materialized variable"
			);
			variable->second->addDeclaredTypeConstraintReference(reference);
		}
		return;
	}
	if (parent) {
		parent->searchParentPatterns(context, reference);
		return;
	}
	context.unresolvedVariableReferences[reference->name].push_back(reference);
}

void Section::addPatternReference(PatternReference *reference) {
	patternReferences.push_back(reference);
	incrementUnresolved();
}

void Section::incrementUnresolved() {
	if (unresolvedCount == 0 && parent && type != SectionType::Retain && type != SectionType::Release) {
		parent->incrementUnresolved();
	}
	unresolvedCount++;
}

void Section::decrementUnresolved() {
	unresolvedCount--;
	if (unresolvedCount == 0 && parent && type != SectionType::Retain && type != SectionType::Release) {
		parent->decrementUnresolved();
	}
}

bool Section::isDescendantOf(Section *ancestor) {
	for (Section *s = parent; s; s = s->parent) {
		if (s == ancestor)
			return true;
	}
	return false;
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

Variable *Section::findVariable(const std::string &name, const Range &useRange) {
	requireCompilerInvariant(useRange.line != nullptr, "source-position variable lookup has no source line");
	const std::pair<int, int> usePosition{useRange.line->mergedLineIndex, useRange.start()};
	for (Section *section = this; section; section = section->parent) {
		auto variable = section->variables.find(name);
		if (variable == section->variables.end())
			continue;
		requireCompilerInvariant(variable->second != nullptr, "section variable metadata contains a null variable");
		auto declaration = section->explicitVariableDeclarations.find(name);
		if (declaration == section->explicitVariableDeclarations.end())
			return variable->second;
		const Range &declarationRange = declaration->second;
		requireCompilerInvariant(declarationRange.line != nullptr, "explicit variable declaration has no source position");
		if (std::pair(declarationRange.line->mergedLineIndex, declarationRange.start()) <= usePosition)
			return variable->second;
	}
	return nullptr;
}

Variable *Section::findVariable(const VariableReference *reference) {
	if (!reference)
		return nullptr;
	const VariableReference *definition = reference->definition ? reference->definition : reference;
	for (Section *section = this; section; section = section->parent) {
		auto variable = section->variables.find(definition->name);
		if (variable != section->variables.end() && variable->second && variable->second->definition == definition)
			return variable->second;
	}
	return nullptr;
}
