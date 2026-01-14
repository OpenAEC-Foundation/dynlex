#include "compiler.h"
#include "IndentData.h"
#include "fileFunctions.h"
#include "patternElement.h"
#include "patternTreeNode.h"
#include "sourceFile.h"
#include "stringFunctions.h"
#include <list>
#include <ranges>
#include <regex>

// regex for line terminators - matches each line including its terminator
const std::regex lineWithTerminatorRegex("([^\r\n]*(?:\r\n|\r|\n))|([^\r\n]+$)");

bool compile(const std::string &path, ParseContext &context) {
	// first, read all source files
	return importSourceFile(path, context) && analyzeSections(context) && resolvePatterns(context);
}

bool importSourceFile(const std::string &path, ParseContext &context) {
	std::string text;

	if (!readStringFromFile(path, text)) {
		return false;
	}

	// iterate over lines, each match includes the line terminator
	SourceFile *sourceFile = new SourceFile(path, text);

	std::string_view fileView{sourceFile->content};

	std::cregex_iterator iter(fileView.begin(), fileView.end(), lineWithTerminatorRegex);
	std::cregex_iterator end;
	int sourceFileLineIndex = 0;
	for (; iter != end; ++iter, ++sourceFileLineIndex) {
		std::string_view lineString = fileView.substr(iter->position(), iter->length());
		CodeLine *line = new CodeLine(lineString, sourceFile);
		line->sourceFileLineIndex = sourceFileLineIndex;
		// first, remove comments and trim whitespace from the right

		std::cmatch match;
		std::regex_search(lineString.begin(), lineString.end(), match, std::regex("[\\s]*(?:#[\\S\\s]*)?$"));
		line->rightTrimmedText = lineString.substr(0, match.position());

		// check if the line is an import statement
		if (line->rightTrimmedText.starts_with("import ")) {
			// if so, recursively import the file
			// extract the file path
			std::string_view importPath = line->rightTrimmedText.substr(std::string_view("import ").length());
			if (!importSourceFile((std::string)importPath, context)) {
				return false;
			}
			// this line doesn't need any form of pattern matching
			line->resolved = true;
		}
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
			if (!line->patternText.length())
				line->resolved = true;
		}
		++compiledLineIndex;
	}

	return true;
}

// step 3: loop over code, resolve patterns and build up a pattern tree until
// all patterns are resolved
bool resolvePatterns(ParseContext &context) {
	std::list<PatternReference *> unResolvedPatternReferences;
	std::list<Section *> unResolvedSections;
	context.mainSection->collectPatternReferencesAndSections(unResolvedPatternReferences, unResolvedSections);
	// add the roots
	std::fill(
		std::begin(context.patternTrees), std::end(context.patternTrees), new PatternTreeNode(PatternElement::Type::Other, "")
	);

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
	for (int resolutionIteration = 0; resolutionIteration < context.maxResolutionIterations; resolutionIteration++) {

		// each iteration, we go over all sections first
		std::remove_if(unResolvedSections.begin(), unResolvedSections.end(), [&context](Section *section) {
			// wether all pattern definitions are resolved for this section
			bool allPatternDefinitionsResolved = section->patternDefinitions.size();
			for (auto definition : section->patternDefinitions) {
				// loop over all pattern elements and 'stripe off' parts with
				// variables
				for (auto element : definition->patternElements) {
					if (element.type == PatternElement::Type::VariableLike) {
						if (section->variables.count(element.text)) {
							element.type = PatternElement::Type::Variable;
						}
						// a single pattern element can never become a variable
						else if (definition->patternElements.size() > 1) {
							// this element could possibly become a variable
							// later. we'll have to check again in the next
							// iteration
							allPatternDefinitionsResolved = false;
						}
					}
				}
			}
			// otherwise, we resolved the section before all of the pattern
			// references inside were resolved!
			if (!allPatternDefinitionsResolved) {
				allPatternDefinitionsResolved = true;
				// the normal way of resolution is by checking if all of it's
				// lines are resolved and then marking the section as resolved.
				for (PatternReference *reference : section->patternReferences) {
					if (!reference->resolved) {
						allPatternDefinitionsResolved = false;
					}
				}
			}
			section->resolved = allPatternDefinitionsResolved;
			return section->resolved;
		});

		// then, go over all lines referencing patterns
		std::remove_if(
			unResolvedPatternReferences.begin(), unResolvedPatternReferences.end(), [&context](PatternReference *reference) {
			// search the pattern tree for this line's pattern

			PatternMatch *match = context.match(reference);
			if (match) {
				reference->resolved = true;
			}

			return reference->resolved;
		}
		);
		if (unResolvedSections.size() == 0 && unResolvedPatternReferences.size() == 0) {
			//all patterns have been successfully resolved
			return true;
		}
	}
	// some patterns couldn't be resolved
	for (PatternReference *reference : unResolvedPatternReferences) {
		context.diagnostics.push_back(
			Diagnostic(Diagnostic::Level::Error, "This pattern couldn't be resolved", reference->range)
		);
	}
	return false;
}