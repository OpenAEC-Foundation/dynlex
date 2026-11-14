#include "compiler.h"
#include "IndentData.h"
#include "classSection.h"
#include "codegen/codegen.h"
#include "compileTimeValue.h"
#include "compilerUtils.h"
#include "dataSections.h"
#include "expression.h"
#include "intrinsicInfo.h"
#include "lsp/sourceFile.h"
#include "pattern/patternDefinition.h"
#include "pattern/patternReference.h"
#include "pattern/pattern_tree/patternElement.h"
#include "pattern/pattern_tree/patternMatch.h"
#include "pattern/pattern_tree/patternTreeNode.h"
#include "pattern/transformedPattern.h"
#include "section/parseUtils.h"
#include "section/scopeRulesSection.h"
#include "section/variable.h"
#include "sourcePreprocessing.h"
#include "sourceSyntax.h"
#include "sourceTransform.h"
#include "stringFunctions.h"
#include "syntaxConfig.h"
#include "type.h"
#include "llvm/ADT/ScopeExit.h"
#include <cassert>
#include <cctype>
#include <chrono>
#include <climits>
#include <cmath>
using namespace std::literals;

static void collectSectionsPreorder(Section *section, std::vector<Section *> &outSections) {
	if (!section)
		return;
	outSections.push_back(section);
	for (Section *child : section->children)
		collectSectionsPreorder(child, outSections);
}

static std::string formatCompileTimeValueForPurityReport(const CompileTimeValue &value) {
	if (const auto *integer = std::get_if<WideInteger>(&value))
		return integer->toString();
	if (const auto *integer = std::get_if<std::uint64_t>(&value))
		return std::to_string(*integer);
	if (std::holds_alternative<std::monostate>(value))
		return "?";
	if (const auto *integer = std::get_if<std::int64_t>(&value))
		return std::to_string(*integer);
	if (std::holds_alternative<MinimumSignedIntegerMagnitude>(value))
		return "9223372036854775808";
	if (const auto *number = std::get_if<double>(&value)) {
		double integralPart = 0.0;
		if (std::modf(*number, &integralPart) == 0.0)
			return std::to_string(static_cast<long long>(integralPart));
		return std::to_string(*number);
	}
	if (const auto *text = std::get_if<std::string>(&value))
		return "\"" + *text + "\"";
	if (const auto *boolean = std::get_if<bool>(&value))
		return *boolean ? "true" : "false";
	if (const auto *type = std::get_if<TypeReferenceValue>(&value)) {
		return type->type.kind == DataType::Kind::Type ? typeToUserName(type->type.toReferencedType())
													   : typeToUserName(type->type);
	}
	if (const auto *constraint = std::get_if<TypeConstraint>(&value))
		return typeToUserName(*constraint);
	if (const auto *pointer = std::get_if<CompileTimePointer>(&value))
		return "pointer(" + std::to_string(pointer->address) + ")";
	if (const auto *function = std::get_if<CompileTimeFunction>(&value))
		return "function(" + std::string(function->definition->range.subString) + ")";
	crashCompilerBug("unknown compile-time value alternative in purity report");
}

static bool purityReportSectionComesBefore(Section *left, Section *right) {
	PatternDefinition *leftDefinition =
		(left && !left->patternDefinitions.empty()) ? left->patternDefinitions.front() : nullptr;
	PatternDefinition *rightDefinition =
		(right && !right->patternDefinitions.empty()) ? right->patternDefinitions.front() : nullptr;
	if (leftDefinition == rightDefinition)
		return left < right;
	if (!leftDefinition)
		return false;
	if (!rightDefinition)
		return true;
	CodeLine *leftLine = leftDefinition->range.line;
	CodeLine *rightLine = rightDefinition->range.line;
	std::string leftUri = (leftLine && leftLine->sourceFile) ? leftLine->sourceFile->uri : "";
	std::string rightUri = (rightLine && rightLine->sourceFile) ? rightLine->sourceFile->uri : "";
	if (leftUri != rightUri)
		return leftUri < rightUri;
	int leftLineIndex = leftLine ? leftLine->sourceFileLineIndex : INT_MAX;
	int rightLineIndex = rightLine ? rightLine->sourceFileLineIndex : INT_MAX;
	if (leftLineIndex != rightLineIndex)
		return leftLineIndex < rightLineIndex;
	if (leftDefinition->range.start() != rightDefinition->range.start())
		return leftDefinition->range.start() < rightDefinition->range.start();
	return left < right;
}

std::string renderPurityReport(ParseContext &context) {
	std::vector<Section *> sections;
	collectSectionsPreorder(context.mainSection, sections);
	std::vector<Section *> functionSections;
	for (Section *section : sections) {
		if (!section || section->type != SectionType::Function || section->isFlex || section->patternDefinitions.empty() ||
			section->instantiations.empty())
			continue;
		functionSections.push_back(section);
	}
	std::sort(functionSections.begin(), functionSections.end(), purityReportSectionComesBefore);
	std::string report;
	for (Section *section : functionSections) {
		PatternDefinition *definition = section->patternDefinitions.front();
		for (const auto &[key, instantiation] : section->instantiations) {
			if (!report.empty())
				report += '\n';
			report += instantiation.purity == InstantiationPurity::Pure ? "pure" : "impure";
			report += " | ";
			report += definition->toString();
			report += " | args=[";
			for (size_t i = 0; i < key.argumentTypes.size(); i++) {
				if (i > 0)
					report += ", ";
				report += typeToUserName(key.argumentTypes[i]);
			}
			report += "]";
			if (!key.compileTimeParameters.empty()) {
				report += " | fixed=[";
				for (size_t i = 0; i < key.compileTimeParameters.size(); i++) {
					if (i > 0)
						report += ", ";
					report += key.compileTimeParameters[i].first;
					report += "=";
					report += formatCompileTimeValueForPurityReport(key.compileTimeParameters[i].second);
				}
				report += "]";
			}
		}
	}
	return report;
}

#include "callableFunctionLookup.inl"

PatternDefinition *findDefinitionBySignature(
	ParseContext &context, SectionType sectionType, std::string_view signature, const lsp::SourceFile *sourceFile
) {
	std::vector<PatternDefinition *> matches = findDefinitionsBySignature(context, sectionType, signature, sourceFile);
	return !matches.empty() ? matches[0] : nullptr;
}

void appendPatternCallBindings(Expression *expr, PatternDefinition *definition, BindingMap &bindings) {
	collectPatternCallBindings(expr, definition, bindings);
}

Expression *createTypeConstraintExpression(ParseContext &context, Section *section, Range sourceRange) {
	if (!section || !sourceRange.line || sourceRange.subString.empty())
		return nullptr;

	Expression *typeExpr = section->detectPatterns(context, sourceRange, SectionType::Function, false);
	if (!typeExpr)
		return nullptr;

	auto materializeTemporaryVariableReferences =
		[&context, section](PatternReference *reference, PatternMatch &match, auto &self) -> void {
		int offset = reference->range().start();
		for (VariableMatch &varMatch : match.discoveredVariables) {
			if (varMatch.variableReference)
				continue;
			varMatch.variableReference = context.createVariableReference(
				Range(reference->range().line, offset + varMatch.lineStartPos, offset + varMatch.lineEndPos), varMatch.name
			);
			Variable *variable = section->findVariable(varMatch.name, varMatch.variableReference->range);
			for (Section *scope = section; !variable && scope; scope = scope->parent) {
				VariableReference *parameter =
					scope->resolvePatternParameterBinding(context, varMatch.name, varMatch.variableReference->range);
				if (parameter)
					variable = scope->findVariable(parameter);
			}
			if (variable) {
				requireCompilerInvariant(variable->definition, "type-constraint binding has no source definition");
				varMatch.variableReference->definition = normalizeBindingReference(variable->definition);
			} else
				context.unboundConstraintVariableScopes.emplace(varMatch.variableReference, section);
		}
		for (PatternMatch &subMatch : match.subMatches)
			self(reference, subMatch, self);
	};

	std::function<void(Expression *)> prepareMatches = [&](Expression *expression) {
		if (!expression)
			return;
		for (Expression *argument : expression->arguments)
			prepareMatches(argument);
		if (expression->kind != Expression::Kind::Pending || !expression->patternReference)
			return;
		PatternReference *reference = expression->patternReference;
		reference->patternElements = getPatternElements(reference->pattern.text);
		if (!reference->match)
			reference->match = context.match(reference);
		if (reference->match)
			materializeTemporaryVariableReferences(reference, *reference->match, materializeTemporaryVariableReferences);
	};

	prepareMatches(typeExpr);
	expandExpression(typeExpr, section);
	visitExpressionTree(typeExpr, [&](Expression *expression) {
		if (expression->kind != Expression::Kind::Variable)
			return false;
		VariableReference *reference = expression->variable;
		requireCompilerInvariant(reference, "type-constraint variable expression has no reference");
		if (!reference->definition && !section->findVariable(reference))
			context.unboundConstraintVariableScopes.try_emplace(reference, section);
		return false;
	});
	return typeExpr;
}

void destroyTypeConstraintExpression(Expression *root) {
	std::unordered_set<Expression *> visited;
	std::function<void(Expression *)> destroy = [&](Expression *expression) {
		if (!expression || !visited.insert(expression).second)
			return;
		for (Expression *argument : expression->arguments)
			destroy(argument);
		delete expression->patternReference;
		delete expression;
	};
	destroy(root);
}

bool compileBeforeTypeInference(const std::string &path, ParseContext &context, const std::function<bool()> &validation) {
	auto stageStart = std::chrono::steady_clock::now();
	size_t stageIndex = 0;
	auto finishStage = [&]() {
		context.frontendStageMilliseconds.at(stageIndex++) =
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stageStart).count();
		stageStart = std::chrono::steady_clock::now();
	};
	llvm::scope_exit unfinishedStage([&]() {
		if (stageIndex < context.frontendStageMilliseconds.size())
			context.frontendStageMilliseconds.at(stageIndex) =
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stageStart).count();
	});
	context.compilationStage = ParseContext::CompilationStage::NotStarted;
	if (!loadSourceFiles(path, context))
		return false;
	if (!preprocessSourceLines(context))
		return false;
	context.compilationStage = ParseContext::CompilationStage::ImportedFiles;
	finishStage();

	if (!analyzeSections(context))
		return false;
	context.compilationStage = ParseContext::CompilationStage::AnalyzedSections;
	finishStage();

	if (!resolvePatterns(context))
		return false;
	if (!prepareDataSectionCalls(context))
		return false;
	context.compilationStage = ParseContext::CompilationStage::ResolvedPatterns;
	finishStage();

	if (!validation())
		return false;
	context.compilationStage = ParseContext::CompilationStage::Validated;

	if (!initializeTargetLayout(context))
		return false;
	finishStage();
	return true;
}

bool compileBeforeTypeInference(const std::string &path, ParseContext &context) {
	return compileBeforeTypeInference(path, context, [&]() {
		return validate(context);
	});
}

bool finishTypeInference(ParseContext &context) {
	requireCompilerInvariant(
		context.compilationStage == ParseContext::CompilationStage::Validated, "type inference requires a validated program"
	);
	if (!inferTypes(context))
		return false;
	context.compilationStage = ParseContext::CompilationStage::InferredTypes;

	return true;
}

bool compile(const std::string &path, ParseContext &context) {
	return compileBeforeTypeInference(path, context) && finishTypeInference(context);
}

// step 2: analyze sections
bool analyzeSections(ParseContext &context) {
	requireCompilerInvariant(
		!context.sourceTransformHooks || context.sourceTransformHooks->sectionLines.size() == context.codeLines.size(),
		"section analysis requires the complete source-product stream"
	);
	SectionAnalysisWork *work = context.sourceTransformHooks && context.sourceTransformHooks->sectionAnalysisEnabled
									? &context.sourceTransformHooks->sectionAnalysis
									: nullptr;
	auto measure = [&](size_t SectionAnalysisWork::*calls, double SectionAnalysisWork::*milliseconds, auto action) {
		if (work)
			++(work->*calls);
		SectionAnalysisTimer timer(work ? &(work->*milliseconds) : nullptr);
		return action();
	};
	IndentData data{};
	Section *currentSection = context.mainSection = new Section(SectionType::Custom);
	int compiledLineIndex = 0;
	auto closeSection = [&](Section *section, int endLineIndex) {
		section->endLineIndex = endLineIndex;
		return measure(&SectionAnalysisWork::finalizations, &SectionAnalysisWork::finalizeMilliseconds, [&]() {
			return section->finalize(context);
		});
	};
	// code lines are added in import order, meaning lines get replaced with
	// code from imported files. we assume that the indent level of the code of
	// imported files and the import statements both match.
	for (CodeLine *line : context.codeLines) {
		line->mergedLineIndex = compiledLineIndex;
		const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
		// skip empty lines (blank or comment-only) for indent tracking
		if (line->rightTrimmedText.empty()) {
			line->section = currentSection;
			currentSection->codeLines.push_back(line);
			line->resolved = true;
			compiledLineIndex++;
			continue;
		}

		int oldIndentLevel = data.indentLevel;
		// check indent level
		SectionLineSyntax lexical = context.sourceTransformHooks
										? context.sourceTransformHooks->sectionLines.at(compiledLineIndex)
										: sectionLineSyntax(*line, syntax);
		std::string_view indentString = line->hasIndentOverride ? std::string_view(line->indentOverride)
																: line->rightTrimmedText.substr(0, lexical.patternStart);
		IndentMeasurement indentation = measureIndent(data, indentString);
		if (!indentation.validAmount) {
			// check amount of indents
			context.addDiagnostic(Diagnostic(
				context, Diagnostic::Level::Error, "invalid indentation amount", Range(line, 0, indentString.length()),
				"expected",
				std::to_string(data.indentString.length() * data.indentLevel) + " " + charName(data.indentString[0]) + "s",
				"found", std::to_string(indentString.length())
			));
		}
		if (indentation.invalidCharacterIndex != std::string::npos) {
			// check type of indent. indentation is only important for section
			// exits, since colons determine section starts.
			char expectedIndentChar = data.indentString[0];
			context.addDiagnostic(Diagnostic(
				context, Diagnostic::Level::Error, "invalid indentation character",
				Range(line, indentation.invalidCharacterIndex, indentString.length()), "expected",
				charName(expectedIndentChar) + "s", "found", "a " + charName(indentString[indentation.invalidCharacterIndex])
			));
		}
		data.indentLevel = indentation.physicalIndentLevel + line->logicalIndentOffset;

		if (data.indentLevel != oldIndentLevel) {
			// section change
			if (data.indentLevel > oldIndentLevel) {
				// cannot go up sections twice in a time
				context.addDiagnostic(Diagnostic(
					context, Diagnostic::Level::Error, "invalid indentation increase", Range(line, 0, indentString.length()),
					"expected",
					std::to_string(data.indentString.length() * oldIndentLevel) + " " + charName(data.indentString[0]) + "s",
					"found", std::to_string(indentString.length())
				));

				// fatal for compilation, since no sections will be made
				return false;
			} else {
				// exit some sections
				for (int popIndentLevel = oldIndentLevel; popIndentLevel != data.indentLevel; popIndentLevel--) {
					Section *closingSection = currentSection;
					currentSection = currentSection->parent;
					if (!closeSection(closingSection, compiledLineIndex + 1))
						return false;
				}
			}
		}

		line->section = currentSection;
		currentSection->codeLines.push_back(line);

		line->patternText = line->rightTrimmedText.substr(lexical.patternStart, lexical.patternLength);

		// check if this line starts a section
		if (lexical.opensSection) {

			// set the current section to the new section for the next line
			currentSection = measure(&SectionAnalysisWork::headersEvaluated, &SectionAnalysisWork::headerMilliseconds, [&]() {
				return currentSection->createSection(context, line);
			});
			if (!currentSection)
				return false;
			currentSection->startLineIndex = compiledLineIndex + 1;
			currentSection->openingLine = line;
			line->sectionOpening = currentSection;
			data.indentLevel++;
		} else {
			if (lexical.importDirective) {
				// Import lines are already processed during importSourceFile;
				// tokenize "import" as a keyword and the path as a string
				line->resolved = true;
			} else if (line->patternText.length()) {
				if (!measure(&SectionAnalysisWork::bodyLinesDispatched, &SectionAnalysisWork::bodyDispatchMilliseconds, [&]() {
					return currentSection->processLine(context, line);
				}))
					return false;
			} else {
				line->resolved = true;
			}
		}
		++compiledLineIndex;
	}
	while (currentSection != context.mainSection) {
		Section *closingSection = currentSection;
		currentSection = currentSection->parent;
		if (!closeSection(closingSection, compiledLineIndex + 1))
			return false;
	}

	return measure(&SectionAnalysisWork::localityPasses, &SectionAnalysisWork::localityMilliseconds, [&]() {
		return applyDefinitionLocalities(context);
	});
}

#include "compilerTypeUtilities.inl"
