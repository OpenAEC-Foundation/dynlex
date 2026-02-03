#include "compiler.h"
#include "IndentData.h"
#include "expression.h"
#include "lsp/fileSystem.h"
#include "lsp/sourceFile.h"
#include "patternElement.h"
#include "patternTreeNode.h"
#include "stringFunctions.h"
#include "variable.h"
#include <algorithm>
#include <list>
#include <ranges>
#include <regex>
using namespace std::literals;

// Find the position of # that's not inside a string literal
// Returns npos if no comment found
size_t findCommentStart(std::string_view line) {
	bool inString = false;
	for (size_t i = 0; i < line.size(); i++) {
		char c = line[i];
		if (c == '"' && (i == 0 || line[i - 1] != '\\')) {
			inString = !inString;
		} else if (c == '#' && !inString) {
			return i;
		}
	}
	return std::string_view::npos;
}

// regex for line terminators - matches each line including its terminator
const std::regex lineWithTerminatorRegex("([^\r\n]*(?:\r\n|\r|\n))|([^\r\n]+$)");

bool compile(const std::string &path, ParseContext &context) {
	// first, read all source files
	return importSourceFile(path, context) && analyzeSections(context) && resolvePatterns(context);
}

bool importSourceFile(const std::string &path, ParseContext &context) {
	// Check if already imported (circular import protection)
	if (context.importedFiles.contains(path)) {
		return true; // Already processed, skip
	}

	lsp::SourceFile *sourceFile = context.fileSystem->getFile(path);
	if (!sourceFile) {
		if (context.importedFiles.empty()) {
			// If this is the main file, report error
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "couldn't import main file: " + path, Range()));
		}
		return false;
	}

	context.importedFiles[path] = sourceFile;

	// iterate over lines, each match includes the line terminator
	std::string_view fileView{sourceFile->content};

	std::cregex_iterator iter(fileView.begin(), fileView.end(), lineWithTerminatorRegex);
	std::cregex_iterator end;
	int sourceFileLineIndex = 0;
	for (; iter != end; ++iter, ++sourceFileLineIndex) {
		std::string_view lineString = fileView.substr(iter->position(), iter->length());
		CodeLine *line = new CodeLine(lineString, sourceFile);
		line->sourceFileLineIndex = sourceFileLineIndex;
		// first, remove comments and trim whitespace from the right
		size_t commentPos = findCommentStart(lineString);
		std::string_view withoutComment =
			(commentPos != std::string_view::npos) ? lineString.substr(0, commentPos) : lineString;

		// trim trailing whitespace
		std::cmatch match;
		std::regex_search(withoutComment.begin(), withoutComment.end(), match, std::regex("[\\s]+$"));
		line->rightTrimmedText = match.empty() ? withoutComment : withoutComment.substr(0, match.position());

		// check if the line is an import statement
		if (line->rightTrimmedText.starts_with("import ")) {
			// if so, recursively import the file
			// extract the file path
			std::string_view importPath = line->rightTrimmedText.substr("import "sv.length());
			if (!importSourceFile((std::string)importPath, context)) {
				context.diagnostics.push_back(Diagnostic(
					Diagnostic::Level::Error, "failed to import source file: " + (std::string)importPath,
					Range(line, "import "sv.length(), line->rightTrimmedText.length())
				));
				return false;
			}
			// this line doesn't need any form of pattern matching
			line->resolved = true;
		}
		line->mergedLineIndex = context.codeLines.size();
		context.codeLines.push_back(line);
	}
	return true;
}

// step 2: analyze sections
bool analyzeSections(ParseContext &context) {
	IndentData data{};
	Section *currentSection = context.mainSection = new Section(SectionType::Custom);
	int compiledLineIndex = 0;
	// code lines are added in import order, meaning lines get replaced with
	// code from imported files. we assume that the indent level of the code of
	// imported files and the import statements both match.
	for (CodeLine *line : context.codeLines) {
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
		std::cmatch match;
		std::regex_search(line->rightTrimmedText.begin(), line->rightTrimmedText.end(), match, std::regex("^(\\s*)"));
		std::string indentString = match[0];
		if (data.indentString.empty()) {
			data.indentString = indentString;
			data.indentLevel = !indentString.empty();
		} else if (indentString.length() % data.indentString.length() != 0) {
			// check amount of indents
			context.diagnostics.push_back(Diagnostic(
				Diagnostic::Level::Error,
				"Invalid indentation! expected " + std::to_string(data.indentString.length() * data.indentLevel) + " " +
					charName(data.indentString[0]) + "s, but found " + std::to_string(indentString.length()),
				Range(line, 0, indentString.length())
			));
		}
		// check type of indent. indentation is only important for section
		// exits, since colons determine section starts.
		if (indentString.length()) {
			char expectedIndentChar = data.indentString[0];
			size_t invalidCharIndex = indentString.find_first_not_of(expectedIndentChar);
			if (invalidCharIndex != std::string::npos) {
				context.diagnostics.push_back(Diagnostic(
					Diagnostic::Level::Error,
					"Invalid indentation! expected only " + charName(expectedIndentChar) + "s, but found a " +
						charName(indentString[invalidCharIndex]),
					Range(line, invalidCharIndex, indentString.length())
				));
			} else {
				data.indentLevel = indentString.length() / data.indentString.length();
			}
		} else {
			data.indentString = "";
			data.indentLevel = 0;
		}

		if (data.indentLevel != oldIndentLevel) {
			// section change
			if (data.indentLevel > oldIndentLevel) {
				// cannot go up sections twice in a time
				context.diagnostics.push_back(Diagnostic(
					Diagnostic::Level::Error,
					"Invalid indentation! expected at max " + std::to_string(data.indentString.length() * oldIndentLevel) +
						" " + charName(data.indentString[0]) + "s, but found " + std::to_string(indentString.length()),
					Range(line, 0, indentString.length())
				));

				// fatal for compilation, since no sections will be made
				return false;
			} else {
				// exit some sections
				for (int popIndentLevel = oldIndentLevel; popIndentLevel != data.indentLevel; popIndentLevel--) {
					currentSection = currentSection->parent;
					currentSection->endLineIndex = compiledLineIndex + 1;
				}
			}
		}

		line->section = currentSection;
		currentSection->codeLines.push_back(line);

		std::string_view trimmedText = line->rightTrimmedText.substr(indentString.length());

		// check if this line starts a section
		if (trimmedText.ends_with(":")) {
			line->patternText = trimmedText.substr(0, trimmedText.length() - 1);

			// set the current section to the new section for the next line
			currentSection = currentSection->createSection(context, line);
			if (!currentSection)
				return false;
			currentSection->startLineIndex = compiledLineIndex + 1;
			line->sectionOpening = currentSection;
			data.indentLevel++;
		} else {
			line->patternText = trimmedText;
			if (line->patternText.length()) {
				currentSection->processLine(context, line);
			} else {
				line->resolved = true;
			}
		}
		++compiledLineIndex;
	}

	return true;
}

void addVariableReferencesFromMatch(ParseContext &context, PatternReference *reference, PatternMatch &match) {
	for (VariableMatch &varMatch : match.discoveredVariables) {
		VariableReference *varRef =
			new VariableReference(Range(reference->range().line, varMatch.lineStartPos, varMatch.lineEndPos), varMatch.name);
		varMatch.variableReference = varRef;
		reference->range().section()->addVariableReference(context, varRef);
	}
	for (PatternMatch &subMatch : match.subMatches) {
		addVariableReferencesFromMatch(context, reference, subMatch);
	}
}

void expandMatch(Expression *rootExpression, Expression *expr, PatternMatch *match) {
	// arguments will be divided over child matches. all arguments will still be present. it's safe to overwrite the main
	// expressions' arguments, since they're pointers.
	expr->arguments = match->arguments;
	// move arguments to the appropriate submatches
	expr->kind = Expression::Kind::PatternCall;
	expr->patternMatch = match;
	for (const PatternMatch &subMatch : match->subMatches) {
		Expression *arg = new Expression();
		arg->range = Range(
			expr->range.line, rootExpression->range.start() + subMatch.lineStartPos,
			rootExpression->range.start() + subMatch.lineEndPos
		);
		expandMatch(rootExpression, arg, const_cast<PatternMatch *>(&subMatch));
		expr->arguments.push_back(arg);
	}

	// Handle discoveredVariables - add Variable expressions using stored references
	for (const VariableMatch &varMatch : match->discoveredVariables) {
		Expression *arg = new Expression();
		arg->kind = Expression::Kind::Variable;
		arg->variable = varMatch.variableReference;
		arg->range = varMatch.variableReference->range;
		expr->arguments.push_back(arg);
	}
}

// Recursively expand pending expressions to their resolved forms
void expandExpression(Expression *expr, Section *section) {
	if (!expr)
		return;

	// Expand children first
	for (Expression *arg : expr->arguments) {
		expandExpression(arg, section);
	}

	// If this is a pending expression, resolve it
	// we can assume that expr->patternReference is set, since pending expressions are only expanded after complete pattern
	// resolution
	if (expr->kind == Expression::Kind::Pending) {
		PatternReference *ref = expr->patternReference;
		if (ref->match) {
			expandMatch(expr, expr, ref->match);
		} else if (ref->patternElements.size() == 1 && ref->patternElements[0].type == PatternElement::Type::Variable) {
			// Resolved to a variable reference
			expr->kind = Expression::Kind::Variable;
			// Find the variable reference in the section
			std::string varName = ref->patternElements[0].text;
			auto it = section->variableReferences.find(varName);
			if (it != section->variableReferences.end() && !it->second.empty()) {
				expr->variable = it->second.front();
			}
		} else if (expr->arguments.size() == 1 && expr->arguments[0]->kind == Expression::Kind::IntrinsicCall) {
			// If the pattern is just an argument placeholder and we have a single intrinsic call,
			// promote the intrinsic to be this expression
			Expression *intrinsic = expr->arguments[0];
			expr->kind = intrinsic->kind;
			expr->intrinsicName = intrinsic->intrinsicName;
			expr->arguments = intrinsic->arguments;
			expr->range = intrinsic->range;
		}
	}
}

// step 3: loop over code, resolve patterns and build up a pattern tree until all patterns are resolved
bool resolvePatterns(ParseContext &context) {
	std::list<PatternReference *> unResolvedPatternReferences;
	std::list<Section *> unResolvedSections;
	context.mainSection->collectPatternReferencesAndSections(unResolvedPatternReferences, unResolvedSections);
	for (Section *unResolvedSection : unResolvedSections) {
		for (PatternDefinition *unresolvedDefinition : unResolvedSection->patternDefinitions) {
			unresolvedDefinition->patternElements = getPatternElements(unresolvedDefinition->range.subString);
		}
	}
	for (PatternReference *unResolvedPatternReference : unResolvedPatternReferences) {

		unResolvedPatternReference->patternElements = getPatternElements(unResolvedPatternReference->pattern.text);
	}
	// add the roots
	std::generate(std::begin(context.patternTrees), std::end(context.patternTrees), []() {
		return new PatternTreeNode(PatternElement::Type::Other, "");
	});

	// solving all patterns and variables is like solving a sudoku. let's get
	// the 'starting numbers' first: those are the patterns which only consist
	// of one element. std::remove_if(unResolvedSections.begin(),
	// unResolvedSections.end(), [context](Section *section)
	//			   {
	//				section->patternDefinitions
	//				line->patternElements =
	// getPatternElements(line->patternText); 				if(line->isPatternDefinition() &&
	// line->patternElements.size() == 1)
	//				{
	//					//this single element can never be a variable, so we can
	// declare this pattern definition as resolved.
	//					line->sectionOpening->resolved = true;
	//					line->resolved = true;
	//					//add the pattern to its respective pattern tree
	//					context.patternTrees[(int)line->sectionOpening->type]->addPatternPart(line->patternElements,
	// line->sectionOpening);
	//				}
	//				return line->resolved; });

	// now start iterating and resolving.
	for (int resolutionIteration = 0; resolutionIteration < context.options.maxResolutionIterations; resolutionIteration++) {

		// each iteration, we go over all sections first
		std::erase_if(unResolvedSections, [&context](Section *section) {
			// wether all pattern definitions are resolved for this section
			section->patternDefinitionsResolved = true;
			for (PatternDefinition *definition : section->patternDefinitions) {
				if (!definition->resolved) {
					definition->resolved = true;
					// loop over all pattern elements and 'stripe off' parts with
					// variables
					for (PatternElement element : definition->patternElements) {
						if (element.type == PatternElement::Type::VariableLike) {
							// a single pattern element can never become a variable
							if (definition->patternElements.size() > 1) {
								// this element could possibly become a variable
								// later. we'll have to check again in the next
								// iteration
								definition->resolved = false;
								section->patternDefinitionsResolved = false;
							}
						}
					}
					if (definition->resolved) {
						// we can add this definition already, to help resolve more references
						context.patternTrees[(size_t)section->type]->addPatternPart(definition->patternElements, definition);
					}
				}
			}
			// otherwise, we resolved the section before all of the pattern
			// references inside were resolved!
			if (!section->patternDefinitionsResolved) {
				// check if all pattern references (including children) are resolved
				section->patternDefinitionsResolved = section->unresolvedCount == 0;
			}
			if (section->patternDefinitionsResolved) {
				for (PatternDefinition *definition : section->patternDefinitions) {
					// add all unresolved definitions to the pattern tree
					if (!definition->resolved) {
						definition->resolved = true;

						context.patternTrees[(size_t)section->type]->addPatternPart(definition->patternElements, definition);
					}
					// add argument
				}
			}
			return section->patternDefinitionsResolved;
		});

		// then, go over all lines referencing patterns
		std::erase_if(unResolvedPatternReferences, [&context](PatternReference *reference) {
			// search the pattern tree for this line's pattern

			PatternMatch *match = context.match(reference);
			if (match) {
				reference->resolve(match);
				addVariableReferencesFromMatch(context, reference, *match);
			} else if (reference->patternElements.size() == 1 &&
					   reference->patternElements[0].type == PatternElement::Type::VariableLike) {
				// since there's no pattern definition matching this, this has to be a variable.
				reference->patternElements[0].type = PatternElement::Type::Variable;
				reference->resolve();
				reference->range().section()->addVariableReference(
					context, new VariableReference(reference->range(), reference->patternElements[0].text)
				);
			}

			return reference->resolved;
		});
		if (unResolvedSections.size() == 0 && unResolvedPatternReferences.size() == 0) {
			// all patterns have been successfully resolved
			// Expand all pending expressions to their resolved forms
			for (CodeLine *line : context.codeLines) {
				if (line->expression) {
					expandExpression(line->expression, line->section);
				}
			}
			// finally, resolve all unresolved variable references
			for (auto &[name, references] : context.unresolvedVariableReferences) {
				// find highest section for each section that has this variable
				std::unordered_map<Section *, Section *> sectionToHighest;
				for (VariableReference *ref : references) {
					Section *sec = ref->range.section();
					if (sectionToHighest.count(sec))
						continue;

					Section *highest = sec;
					for (Section *a = sec->parent; a; a = a->parent) {
						if (a->variableReferences.count(name))
							highest = a;
					}
					sectionToHighest[sec] = highest;
				}

				// group references by their highest section
				std::unordered_map<Section *, std::vector<VariableReference *>> groups;
				for (VariableReference *ref : references) {
					groups[sectionToHighest[ref->range.section()]].push_back(ref);
				}

				// process each group
				for (auto &[highestSection, groupRefs] : groups) {
					// find first reference by merged line index (becomes definition)
					VariableReference *definition = *std::min_element(groupRefs.begin(), groupRefs.end(), [](auto *a, auto *b) {
						return a->range.line->mergedLineIndex < b->range.line->mergedLineIndex;
					});

					// store definition in its section's definitions list
					definition->range.section()->variableDefinitions[name] = definition;
					// create Variable in highest section
					highestSection->variables[name] = new Variable(name, definition);

					// link all references to the definition
					for (VariableReference *ref : groupRefs) {
						if (ref != definition)
							ref->definition = definition;
					}
				}
			}
			return true;
		}
	}

	// some patterns couldn't be resolved
	for (PatternReference *reference : unResolvedPatternReferences) {
		context.diagnostics.push_back(
			Diagnostic(Diagnostic::Level::Error, "This pattern couldn't be resolved", reference->range())
		);
	}
	return false;
}