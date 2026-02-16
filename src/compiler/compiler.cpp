#include "compiler.h"
#include "IndentData.h"
#include "classSection.h"
#include "expression.h"
#include "intrinsicInfo.h"
#include "lsp/fileSystem.h"
#include "lsp/sourceFile.h"
#include "patternElement.h"
#include "patternTreeNode.h"
#include "stringFunctions.h"
#include "transformedPattern.h"
#include "type.h"
#include "variable.h"
#include <algorithm>
#include <list>
#include <queue>
#include <ranges>
#include <regex>
#include <unordered_set>
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
	return importSourceFile(path, context) && analyzeSections(context) && resolvePatterns(context) && inferTypes(context);
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
			// recursively import the file, replacing this line with the imported content
			std::string_view importPath = line->rightTrimmedText.substr("import "sv.length());
			if (!importSourceFile((std::string)importPath, context)) {
				context.diagnostics.push_back(Diagnostic(
					Diagnostic::Level::Error, "failed to import source file: " + (std::string)importPath,
					Range(line, "import "sv.length(), line->rightTrimmedText.length())
				));
				return false;
			}
			continue;
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

	// Create instantiations for class definitions where all fields have declared types
	std::function<void(Section *)> createDeclaredInstantiations = [&](Section *section) {
		if (section->type == SectionType::Class) {
			auto *classSec = static_cast<ClassSection *>(section);
			ClassDefinition *classDef = classSec->classDefinition;
			if (!classDef->fields.empty() && classDef->instantiations.empty()) {
				bool allDeclared = true;
				std::vector<Type> fieldTypes;
				for (const auto &field : classDef->fields) {
					if (!field.declaredType.isDeduced()) {
						allDeclared = false;
						break;
					}
					fieldTypes.push_back(field.declaredType);
				}
				if (allDeclared) {
					classDef->instantiations.push_back({fieldTypes});
				}
			}
		}
		for (Section *child : section->children)
			createDeclaredInstantiations(child);
	};
	createDeclaredInstantiations(context.mainSection);

	return true;
}

void addVariableReferencesFromMatch(ParseContext &context, PatternReference *reference, PatternMatch &match) {
	int offset = reference->range().start();
	for (VariableMatch &varMatch : match.discoveredVariables) {
		VariableReference *varRef = new VariableReference(
			Range(reference->range().line, offset + varMatch.lineStartPos, offset + varMatch.lineEndPos), varMatch.name
		);
		varMatch.variableReference = varRef;
		reference->range().section()->addVariableReference(context, varRef);
	}
	for (PatternMatch &subMatch : match.subMatches) {
		addVariableReferencesFromMatch(context, reference, subMatch);
	}
}

void expandMatch(Expression *rootExpression, Expression *expr, PatternMatch *match) {
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

	// Handle discoveredWords - add string Literal expressions
	for (const WordMatch &wordMatch : match->discoveredWords) {
		Expression *arg = new Expression();
		arg->kind = Expression::Kind::Literal;
		arg->literalValue = wordMatch.text;
		arg->range = Range(
			rootExpression->range.line, rootExpression->range.start() + wordMatch.lineStartPos,
			rootExpression->range.start() + wordMatch.lineEndPos
		);
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

// Collect all body references from a definition section's descendants (including through nested definitions,
// since nested code can access parent parameters).
static void collectBodyReferences(Section *section, std::vector<PatternReference *> &refs) {
	for (Section *child : section->children) {
		refs.insert(refs.end(), child->patternReferences.begin(), child->patternReferences.end());
		collectBodyReferences(child, refs);
	}
}

// Compute initial variableLikeCounts for each definition section.
static void computeVariableLikeCounts(std::list<Section *> &sections) {
	for (Section *section : sections) {
		std::vector<PatternReference *> bodyRefs;
		collectBodyReferences(section, bodyRefs);

		// Collect all VL texts from all definitions in this section
		std::unordered_set<std::string> vlTexts;
		for (PatternDefinition *def : section->patternDefinitions) {
			forEachLeafElement(def->patternElements, [&](PatternElement &elem) {
				if (elem.type == PatternElement::Type::VariableLike)
					vlTexts.insert(elem.text);
			});
		}

		// Count body references that contain each VL text
		for (const std::string &vlText : vlTexts) {
			int count = 0;
			for (PatternReference *ref : bodyRefs) {
				for (const PatternElement &refElem : ref->patternElements) {
					if (refElem.type == PatternElement::Type::VariableLike && refElem.text == vlText) {
						count++;
						break;
					}
				}
			}
			section->variableLikeCounts[vlText] = count;
		}
	}
}

// After a body reference resolves, decrement VL counts on all ancestor definition sections
// (nested code can access parent parameters).
static void decrementVariableLikeCounts(PatternReference *reference) {
	Section *sec = reference->range().section();
	while (sec) {
		if (!sec->patternDefinitions.empty()) {
			for (const PatternElement &refElem : reference->patternElements) {
				if (refElem.type == PatternElement::Type::VariableLike) {
					auto it = sec->variableLikeCounts.find(refElem.text);
					if (it != sec->variableLikeCounts.end() && it->second > 0)
						it->second--;
				}
			}
		}
		sec = sec->parent;
	}
}

// Recursively record which definition each reference matched (including sub-match definitions)
static void trackMatchDefinitions(
	PatternMatch &match, PatternReference *reference,
	std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> &defToRefs
) {
	if (match.matchedEndNode && match.matchedEndNode->matchingDefinition) {
		defToRefs[match.matchedEndNode->matchingDefinition].push_back(reference);
	}
	for (PatternMatch &subMatch : match.subMatches) {
		trackMatchDefinitions(subMatch, reference, defToRefs);
	}
}

// Inverse of decrementVariableLikeCounts — re-increment VL counts for a reference being un-resolved
static void incrementVariableLikeCounts(PatternReference *reference) {
	Section *sec = reference->range().section();
	while (sec) {
		if (!sec->patternDefinitions.empty()) {
			for (const PatternElement &refElem : reference->patternElements) {
				if (refElem.type == PatternElement::Type::VariableLike) {
					auto it = sec->variableLikeCounts.find(refElem.text);
					if (it != sec->variableLikeCounts.end())
						it->second++;
				}
			}
		}
		sec = sec->parent;
	}
}

// Remove VariableReferences created from a match, undoing addVariableReferencesFromMatch and searchParentPatterns effects.
// If any ancestor definitions had VL→Variable promotions reverted, their sections are added to affectedSections.
static void removeVariableReferencesFromMatch(
	ParseContext &context, PatternReference *reference, PatternMatch &match, std::unordered_set<Section *> &affectedSections
) {
	Section *refSection = reference->range().section();
	for (VariableMatch &varMatch : match.discoveredVariables) {
		if (!varMatch.variableReference)
			continue;
		const std::string &name = varMatch.variableReference->name;

		// Remove from section's variableReferences
		auto it = refSection->variableReferences.find(name);
		if (it != refSection->variableReferences.end()) {
			auto &vec = it->second;
			vec.erase(std::remove(vec.begin(), vec.end(), varMatch.variableReference), vec.end());
			if (vec.empty())
				refSection->variableReferences.erase(it);
		}

		// Remove from unresolvedVariableReferences
		auto uit = context.unresolvedVariableReferences.find(name);
		if (uit != context.unresolvedVariableReferences.end()) {
			auto &vec = uit->second;
			vec.erase(std::remove(vec.begin(), vec.end(), varMatch.variableReference), vec.end());
			if (vec.empty())
				context.unresolvedVariableReferences.erase(uit);
		}

		// Undo searchParentPatterns effects: if this varRef caused a VL→Variable promotion
		// in an ancestor definition, and no other references to this name remain in the section,
		// revert the element back to VariableLike
		if (varMatch.variableReference->definition) {
			// Check if there are still other references to this name in the section
			bool hasOtherRefs = refSection->variableReferences.contains(name);
			if (!hasOtherRefs) {
				// Walk up parent sections to find the definition that was modified
				for (Section *sec = refSection; sec; sec = sec->parent) {
					// Remove variableDefinitions entry if it was created by searchParentPatterns
					auto defIt = sec->variableDefinitions.find(name);
					if (defIt != sec->variableDefinitions.end()) {
						// Remove the definition VarRef from variableReferences too
						auto vit = sec->variableReferences.find(name);
						if (vit != sec->variableReferences.end()) {
							auto &vec = vit->second;
							vec.erase(std::remove(vec.begin(), vec.end(), defIt->second), vec.end());
							if (vec.empty())
								sec->variableReferences.erase(vit);
						}
						sec->variableDefinitions.erase(defIt);
					}

					// Revert Variable→VariableLike in pattern definitions and mark for re-resolution.
					// Must remove from tree BEFORE changing element types (tree was built with old types).
					for (PatternDefinition *def : sec->patternDefinitions) {
						bool needsReResolution = false;
						forEachLeafElement(def->patternElements, [&](PatternElement &element) {
							if (element.type == PatternElement::Type::Variable && element.text == name)
								needsReResolution = true;
						});
						if (needsReResolution && def->resolved) {
							// Remove from tree while elements still reflect the old path
							SectionType treeType = sec->type == SectionType::Class ? SectionType::Expression : sec->type;
							context.patternTrees[(size_t)treeType]->removePatternPart(def->patternElements, def);
						}
						// Now revert element types
						forEachLeafElement(def->patternElements, [&](PatternElement &element) {
							if (element.type == PatternElement::Type::Variable && element.text == name) {
								element.type = PatternElement::Type::VariableLike;
								def->resolved = false;
								sec->patternDefinitionsResolved = false;
								affectedSections.insert(sec);
							}
						});
					}
					// Stop if we found pattern definitions (that's where searchParentPatterns would have acted)
					if (!sec->patternDefinitions.empty())
						break;
				}
			}
		}

		varMatch.variableReference = nullptr;
	}
	for (PatternMatch &subMatch : match.subMatches) {
		removeVariableReferencesFromMatch(context, reference, subMatch, affectedSections);
	}
}

// Un-resolve a reference: undo all effects and prepare it for re-matching.
// Returns the set of definition sections that had their VL classification affected.
static std::unordered_set<Section *> unresolveReference(
	ParseContext &context, PatternReference *reference,
	std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> &defToRefs
) {
	std::unordered_set<Section *> affectedSections;
	if (!reference->resolved || !reference->match)
		return affectedSections;

	// Remove variable references created from the match
	removeVariableReferencesFromMatch(context, reference, *reference->match, affectedSections);

	// Re-increment VL counts (inverse of decrementVariableLikeCounts done during resolve)
	incrementVariableLikeCounts(reference);

	// Remove from defToRefs tracking
	if (reference->match->matchedEndNode && reference->match->matchedEndNode->matchingDefinition) {
		auto it = defToRefs.find(reference->match->matchedEndNode->matchingDefinition);
		if (it != defToRefs.end()) {
			auto &vec = it->second;
			vec.erase(std::remove(vec.begin(), vec.end(), reference), vec.end());
		}
	}

	// Clear match and mark unresolved
	reference->match = nullptr;
	reference->resolved = false;
	reference->range().section()->incrementUnresolved();

	return affectedSections;
}

// Resolve a list of pattern references against the tree. Returns true if all resolved.
static bool resolveReferences(
	ParseContext &context, std::list<PatternReference *> &references, bool decrementCounts,
	std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> *defToRefs = nullptr
) {
	return std::erase_if(references, [&context, decrementCounts, defToRefs](PatternReference *reference) {
		PatternMatch *match = context.match(reference);
		if (match) {
			reference->resolve(match);
			addVariableReferencesFromMatch(context, reference, *match);
			if (decrementCounts)
				decrementVariableLikeCounts(reference);
			if (defToRefs)
				trackMatchDefinitions(*match, reference, *defToRefs);
		} else if (reference->patternElements.size() == 1 &&
				   reference->patternElements[0].type == PatternElement::Type::VariableLike) {
			reference->patternElements[0].type = PatternElement::Type::Variable;
			reference->resolve();
			reference->range().section()->addVariableReference(
				context, new VariableReference(reference->range(), reference->patternElements[0].text)
			);
			if (decrementCounts)
				decrementVariableLikeCounts(reference);
		}
		return reference->resolved;
	}) > 0;
}

// step 3: loop over code, resolve patterns and build up a pattern tree until all patterns are resolved
bool resolvePatterns(ParseContext &context) {
	std::list<PatternReference *> bodyReferences;
	std::list<PatternReference *> globalReferences;
	std::list<Section *> unResolvedSections;
	context.mainSection->collectPatternReferencesAndSections(bodyReferences, globalReferences, unResolvedSections);
	for (Section *unResolvedSection : unResolvedSections) {
		for (PatternDefinition *unresolvedDefinition : unResolvedSection->patternDefinitions) {
			unresolvedDefinition->patternElements = parsePatternElements(unresolvedDefinition->range.subString);
		}
	}
	for (PatternReference *ref : bodyReferences)
		ref->patternElements = getPatternElements(ref->pattern.text);
	for (PatternReference *ref : globalReferences)
		ref->patternElements = getPatternElements(ref->pattern.text);

	// Compute initial VL counts before resolution
	computeVariableLikeCounts(unResolvedSections);

	// add the roots
	std::generate(std::begin(context.patternTrees), std::end(context.patternTrees), []() {
		return new PatternTreeNode(PatternElement::Type::Other, "");
	});

	// Phase 1: resolve body references and definitions
	std::unordered_map<PatternDefinition *, std::vector<PatternReference *>> definitionToReferences;

	// Helper: after adding a definition to the tree, find less-specific definitions
	// and unresolve any references that matched them so they can re-match the more specific one.
	auto invalidateStaleMatches = [&](PatternDefinition *definition, SectionType treeType) {
		auto lessSpecific = context.patternTrees[(size_t)treeType]->findLessSpecificDefinitions(definition->patternElements);
		for (PatternDefinition *lessDef : lessSpecific) {
			auto it = definitionToReferences.find(lessDef);
			if (it == definitionToReferences.end())
				continue;
			// Copy the vector since unresolveReference modifies it
			std::vector<PatternReference *> refs = it->second;
			for (PatternReference *ref : refs) {
				if (!ref->resolved)
					continue;
				auto affectedSections = unresolveReference(context, ref, definitionToReferences);
				bodyReferences.push_back(ref);
				// If any ancestor definitions had VL→Variable reverted, re-add their sections
				// for re-resolution so they can re-classify correctly
				for (Section *sec : affectedSections) {
					if (std::find(unResolvedSections.begin(), unResolvedSections.end(), sec) == unResolvedSections.end()) {
						unResolvedSections.push_back(sec);
					}
				}
			}
		}
	};

	// Helper: add a definition to the pattern tree and emit a diagnostic if a duplicate exists.
	auto addDefinitionToTree = [&](PatternDefinition *definition, SectionType treeType) {
		PatternDefinition *existing =
			context.patternTrees[(size_t)treeType]->addPatternPart(definition->patternElements, definition);
		if (existing) {
			context.diagnostics.push_back(Diagnostic(
				Diagnostic::Level::Error,
				"Duplicate pattern definition (conflicts with definition at " + existing->range.toString() + ")",
				definition->range
			));
		}
		invalidateStaleMatches(definition, treeType);
	};

	for (int resolutionIteration = 0; resolutionIteration < context.options.maxResolutionIterations; resolutionIteration++) {

		bool madeProgress = false;
		size_t sectionsBefore = unResolvedSections.size();

		// each iteration, we go over all sections first
		std::erase_if(unResolvedSections, [&](Section *section) {
			section->patternDefinitionsResolved = true;
			for (PatternDefinition *definition : section->patternDefinitions) {
				if (!definition->resolved) {
					definition->resolved = true;
					forEachLeafElement(definition->patternElements, [&](PatternElement &element) {
						if (element.type == PatternElement::Type::VariableLike) {
							if (definition->patternElements.size() > 1) {
								if (section->variableLikeCounts[element.text] == 0) {
									element.type = PatternElement::Type::Other;
								} else {
									definition->resolved = false;
									section->patternDefinitionsResolved = false;
								}
							}
						}
					});
					if (definition->resolved) {
						SectionType treeType = section->type == SectionType::Class ? SectionType::Expression : section->type;
						addDefinitionToTree(definition, treeType);
					}
				}
			}
			if (!section->patternDefinitionsResolved) {
				section->patternDefinitionsResolved = section->unresolvedCount == 0;
			}
			if (section->patternDefinitionsResolved) {
				for (PatternDefinition *definition : section->patternDefinitions) {
					if (!definition->resolved) {
						definition->resolved = true;
						SectionType treeType = section->type == SectionType::Class ? SectionType::Expression : section->type;
						addDefinitionToTree(definition, treeType);
					}
				}
			}
			return section->patternDefinitionsResolved;
		});

		if (unResolvedSections.size() < sectionsBefore)
			madeProgress = true;

		if (resolveReferences(context, bodyReferences, true, &definitionToReferences))
			madeProgress = true;

		if (unResolvedSections.empty() && bodyReferences.empty())
			break;

		// Deadlock detection: if no progress was made, we have a cycle (self-recursion,
		// mutual recursion, or any dependency loop). Break the cycle by force-resolving
		// all remaining sections — their unresolved VL elements become parameters.
		// The cyclic body references will then resolve against the newly-added definitions
		// in the next iteration.
		if (!madeProgress) {
			if (unResolvedSections.empty())
				break; // no sections to force-resolve and no progress — truly stuck
			for (Section *section : unResolvedSections) {
				section->patternDefinitionsResolved = true;
				for (PatternDefinition *definition : section->patternDefinitions) {
					if (!definition->resolved) {
						definition->resolved = true;
						SectionType treeType = section->type == SectionType::Class ? SectionType::Expression : section->type;
						addDefinitionToTree(definition, treeType);
					}
				}
			}
			unResolvedSections.clear();
		}
	}

	// Phase 2: resolve global references (all definitions are now in the tree)
	for (int resolutionIteration = 0; resolutionIteration < context.options.maxResolutionIterations; resolutionIteration++) {
		resolveReferences(context, globalReferences, false);
		if (globalReferences.empty())
			break;
	}

	if (!unResolvedSections.empty() || !bodyReferences.empty() || !globalReferences.empty()) {
		for (Section *sec : unResolvedSections) {
			for (PatternDefinition *def : sec->patternDefinitions) {
				context.diagnostics.push_back(Diagnostic(
					Diagnostic::Level::Error,
					"This pattern definition couldn't be resolved: " + (std::string)def->range.subString, def->range
				));
			}
		}
		for (PatternReference *reference : bodyReferences)
			context.diagnostics.push_back(
				Diagnostic(Diagnostic::Level::Error, "This pattern couldn't be resolved", reference->range())
			);
		for (PatternReference *reference : globalReferences)
			context.diagnostics.push_back(
				Diagnostic(Diagnostic::Level::Error, "This pattern couldn't be resolved", reference->range())
			);
		return false;
	}

	// Phase 3: Resolve precedence declarations and re-match affected references
	{
		// Resolve before:/after: signature strings to pattern definitions in the expression trie
		auto resolveSignature = [&](const std::string &signature) -> PatternDefinition * {
			std::string converted = signature;
			for (char &c : converted) {
				if (c == '$')
					c = argumentChar;
			}
			auto elements = getPatternElements(converted);
			PatternTreeNode *node = context.patternTrees[(int)SectionType::Expression];
			for (const auto &elem : elements) {
				if (!node)
					return nullptr;
				if (elem.type == PatternElement::Type::Variable) {
					node = node->argumentChild;
				} else {
					auto it = node->literalChildren.find(elem.text);
					node = (it != node->literalChildren.end()) ? it->second : nullptr;
				}
			}
			return node ? node->matchingDefinition : nullptr;
		};

		// Collect precedence edges: higher → lower (higher prec = evaluated first)
		struct PrecedenceEdge {
			PatternDefinition *higher;
			PatternDefinition *lower;
		};
		std::vector<PrecedenceEdge> edges;
		std::unordered_set<PatternDefinition *> involvedDefs;

		std::function<bool(Section *)> collectPrecedence = [&](Section *section) -> bool {
			if (!section->beforePatterns.empty() || !section->afterPatterns.empty()) {
				for (PatternDefinition *def : section->patternDefinitions) {
					involvedDefs.insert(def);

					// before: B means "this definition evaluates before B" = higher precedence
					for (const std::string &beforeStr : section->beforePatterns) {
						PatternDefinition *target = resolveSignature(beforeStr);
						if (!target) {
							context.diagnostics.push_back(
								Diagnostic(Diagnostic::Level::Error, "Precedence target not found: " + beforeStr, def->range)
							);
							return false;
						}
						involvedDefs.insert(target);
						edges.push_back({def, target});
					}

					// after: A means "this definition evaluates after A" = lower precedence
					for (const std::string &afterStr : section->afterPatterns) {
						PatternDefinition *target = resolveSignature(afterStr);
						if (!target) {
							context.diagnostics.push_back(
								Diagnostic(Diagnostic::Level::Error, "Precedence target not found: " + afterStr, def->range)
							);
							return false;
						}
						involvedDefs.insert(target);
						edges.push_back({target, def});
					}
				}
			}
			for (Section *child : section->children)
				if (!collectPrecedence(child))
					return false;
			return true;
		};

		if (!collectPrecedence(context.mainSection))
			return false;

		if (!involvedDefs.empty()) {
			// Topological sort (Kahn's algorithm) to assign precedence levels
			std::unordered_map<PatternDefinition *, std::vector<PatternDefinition *>> adjList;
			std::unordered_map<PatternDefinition *, int> inDegree;
			for (PatternDefinition *def : involvedDefs)
				inDegree[def] = 0;
			for (auto &edge : edges) {
				adjList[edge.higher].push_back(edge.lower);
				inDegree[edge.lower]++;
			}

			std::queue<PatternDefinition *> topoQueue;
			for (auto &[def, deg] : inDegree) {
				if (deg == 0)
					topoQueue.push(def);
			}

			// BFS wave-based topological sort: nodes in the same wave get the same precedence level.
			// This ensures operators like * and / (no edge between them) share the same level,
			// enforcing left-to-right associativity for same-precedence operators.
			size_t processedCount = 0;
			int currentLevel = (int)involvedDefs.size();
			while (!topoQueue.empty()) {
				size_t waveSize = topoQueue.size();
				for (size_t i = 0; i < waveSize; i++) {
					PatternDefinition *def = topoQueue.front();
					topoQueue.pop();
					def->precedence = currentLevel;
					processedCount++;
					for (PatternDefinition *lower : adjList[def]) {
						if (--inDegree[lower] == 0)
							topoQueue.push(lower);
					}
				}
				currentLevel--;
			}

			if (processedCount != involvedDefs.size()) {
				context.diagnostics.push_back(
					Diagnostic(Diagnostic::Level::Error, "Cycle detected in precedence declarations", Range())
				);
				return false;
			}

			// Re-match body references that involve operators with precedence
			std::function<bool(PatternMatch &)> matchInvolvesPrecedence = [&](PatternMatch &match) -> bool {
				if (match.matchedEndNode && match.matchedEndNode->matchingDefinition &&
					match.matchedEndNode->matchingDefinition->precedence > 0)
					return true;
				for (PatternMatch &sub : match.subMatches)
					if (matchInvolvesPrecedence(sub))
						return true;
				return false;
			};

			// Helper to remove variable references from a match (simplified, no VL count changes)
			std::function<void(PatternReference *, PatternMatch &)> removeMatchVarRefs = [&](PatternReference *reference,
																							 PatternMatch &match) {
				Section *refSection = reference->range().section();
				for (VariableMatch &varMatch : match.discoveredVariables) {
					if (!varMatch.variableReference)
						continue;
					const std::string &name = varMatch.variableReference->name;
					auto it = refSection->variableReferences.find(name);
					if (it != refSection->variableReferences.end()) {
						auto &vec = it->second;
						vec.erase(std::remove(vec.begin(), vec.end(), varMatch.variableReference), vec.end());
						if (vec.empty())
							refSection->variableReferences.erase(it);
					}
					auto uit = context.unresolvedVariableReferences.find(name);
					if (uit != context.unresolvedVariableReferences.end()) {
						auto &vec = uit->second;
						vec.erase(std::remove(vec.begin(), vec.end(), varMatch.variableReference), vec.end());
						if (vec.empty())
							context.unresolvedVariableReferences.erase(uit);
					}
					varMatch.variableReference = nullptr;
				}
				for (PatternMatch &sub : match.subMatches)
					removeMatchVarRefs(reference, sub);
			};

			// Collect all pattern references from all sections
			std::vector<PatternReference *> allRefs;
			std::function<void(Section *)> collectRefs = [&](Section *section) {
				allRefs.insert(allRefs.end(), section->patternReferences.begin(), section->patternReferences.end());
				for (Section *child : section->children)
					collectRefs(child);
			};
			collectRefs(context.mainSection);

			// Re-match references that involve operators with precedence
			for (PatternReference *ref : allRefs) {
				if (!ref->resolved || !ref->match)
					continue;
				if (!matchInvolvesPrecedence(*ref->match))
					continue;

				// Remove variable references from old match
				removeMatchVarRefs(ref, *ref->match);

				// Re-match with precedence constraints active
				PatternMatch *newMatch = context.match(ref);
				ref->match = newMatch;
				if (newMatch)
					addVariableReferencesFromMatch(context, ref, *newMatch);
			}
		}
	}

	// All patterns resolved — expand expressions and resolve variable references
	for (CodeLine *line : context.codeLines) {
		if (line->expression)
			expandExpression(line->expression, line->section);
	}
	for (auto &[name, references] : context.unresolvedVariableReferences) {
		bool isGlobal = context.declaredGlobalVariables.contains(name);

		std::unordered_map<Section *, Section *> sectionToHighest;
		for (VariableReference *ref : references) {
			Section *sec = ref->range.section();
			if (sectionToHighest.contains(sec))
				continue;
			Section *highest = sec;

			// Find the enclosing function (Expression/Effect section)
			Section *functionScope = nullptr;
			for (Section *a = sec; a; a = a->parent) {
				if ((a->type == SectionType::Expression || a->type == SectionType::Effect) && !a->isMacro) {
					functionScope = a;
					break;
				}
			}

			// Check if THIS function declares the variable as global
			bool declaredGlobalHere = false;
			if (functionScope) {
				for (const std::string &globalVar : functionScope->globalVariables) {
					if (globalVar == name) {
						declaredGlobalHere = true;
						break;
					}
				}
			}

			for (Section *a = sec->parent; a; a = a->parent) {
				// Stop at function boundary unless this function declares it as global
				if (!declaredGlobalHere && a == functionScope) {
					break;
				}
				if (a->variableReferences.contains(name))
					highest = a;
			}
			sectionToHighest[sec] = highest;
		}
		std::unordered_map<Section *, std::vector<VariableReference *>> groups;
		for (VariableReference *ref : references)
			groups[sectionToHighest[ref->range.section()]].push_back(ref);
		for (auto &[highestSection, groupRefs] : groups) {
			VariableReference *definition = *std::min_element(groupRefs.begin(), groupRefs.end(), [](auto *a, auto *b) {
				return a->range.line->mergedLineIndex < b->range.line->mergedLineIndex;
			});

			// This group is the global one if the variable is declared global and
			// the group's highest section is at the top level (no enclosing function)
			bool groupIsGlobal = false;
			if (isGlobal) {
				groupIsGlobal = true;
				for (Section *a = highestSection; a; a = a->parent) {
					if ((a->type == SectionType::Expression || a->type == SectionType::Effect) && !a->isMacro) {
						groupIsGlobal = false;
						break;
					}
				}
			}

			highestSection->variableDefinitions[name] = definition;
			highestSection->variables[name] = new Variable(name, definition, groupIsGlobal);
			for (VariableReference *ref : groupRefs) {
				if (ref != definition)
					ref->definition = definition;
			}
		}
	}
	return true;
}

// Resolve a variable expression through macro bindings to find the actual variable expression.
// Stops if the binding is self-referential (call-site variable has the same name as the macro parameter).
static Expression *
resolveVarThroughMacro(Expression *expr, const std::unordered_map<std::string, Expression *> &macroBindings) {
	if (!expr || expr->kind != Expression::Kind::Variable || !expr->variable)
		return expr;
	auto it = macroBindings.find(expr->variable->name);
	if (it != macroBindings.end() && it->second != expr)
		return resolveVarThroughMacro(it->second, macroBindings);
	return expr;
}

// Resolve an expression's type through macro bindings
static Type resolveTypeThroughMacro(Expression *expr, const std::unordered_map<std::string, Expression *> &macroBindings) {
	Expression *resolved = resolveVarThroughMacro(expr, macroBindings);
	return resolved ? resolved->type : Type{};
}

// Infer types through a macro body with given parameter bindings. Returns true if anything changed.
static bool
inferMacroBody(Section *macroSection, const std::unordered_map<std::string, Expression *> &bindings, ParseContext &context);

// Infer the type of an expression bottom-up. Returns true if the type changed.
static bool inferExpressionType(
	Expression *expr, ParseContext &context, const std::unordered_map<std::string, Expression *> &macroBindings = {}
) {
	if (!expr)
		return false;

	bool changed = false;

	// Recurse into arguments first (bottom-up)
	for (Expression *arg : expr->arguments) {
		changed |= inferExpressionType(arg, context, macroBindings);
	}

	Type oldType = expr->type;

	switch (expr->kind) {
	case Expression::Kind::Literal: {
		if (std::holds_alternative<int64_t>(expr->literalValue)) {
			expr->type = {Type::Kind::Numeric};
		} else if (std::holds_alternative<double>(expr->literalValue)) {
			expr->type = {Type::Kind::Float, 8}; // C++ double = f64
		} else if (std::holds_alternative<std::string>(expr->literalValue)) {
			expr->type = {Type::Kind::Integer, 1, 1};
		}
		break;
	}

	case Expression::Kind::Variable: {
		if (expr->variable) {
			std::string varName = expr->variable->name;
			// Check macro bindings first
			auto macroIt = macroBindings.find(varName);
			if (macroIt != macroBindings.end()) {
				Type boundType = macroIt->second->type;
				if (boundType.isDeduced()) {
					expr->type = boundType;
				}
				break;
			}
			// Look up variable in scope
			Section *sec = expr->range.line ? expr->range.line->section : nullptr;
			Variable *var = sec ? sec->findVariable(varName) : nullptr;
			if (var && var->type.isDeduced()) {
				expr->type = var->type;
			}
		}
		break;
	}

	case Expression::Kind::IntrinsicCall: {
		const IntrinsicInfo *info = findIntrinsic(expr->intrinsicName);
		if (info) {
			switch (info->returnKind) {
			case IntrinsicReturnKind::SameAsArgs:
				if (info->argCount == 1 && expr->arguments.size() >= 2) {
					Type argType = resolveTypeThroughMacro(expr->arguments[1], macroBindings);
					if (argType.isDeduced())
						expr->type = argType;
				} else if (info->argCount == 2 && expr->arguments.size() >= 3) {
					Type leftType = resolveTypeThroughMacro(expr->arguments[1], macroBindings);
					Type rightType = resolveTypeThroughMacro(expr->arguments[2], macroBindings);
					if (leftType.isDeduced() && rightType.isDeduced()) {
						expr->type = isPointerArithmeticOperator(expr->intrinsicName)
										 ? Type::promoteArithmetic(leftType, rightType)
										 : Type::promote(leftType, rightType);
					}
				}
				break;
			case IntrinsicReturnKind::Bool:
				expr->type = {Type::Kind::Bool};
				break;
			case IntrinsicReturnKind::Void:
				// "store" has side effects on variable types beyond just being Void
				if (expr->intrinsicName == "store" && expr->arguments.size() >= 3) {
					Expression *destExpr = resolveVarThroughMacro(expr->arguments[1], macroBindings);
					Type valType = resolveTypeThroughMacro(expr->arguments[2], macroBindings);
					if (destExpr->kind == Expression::Kind::Variable && destExpr->variable && valType.isDeduced()) {
						Section *sec = destExpr->range.line ? destExpr->range.line->section : nullptr;
						Variable *var = sec ? sec->findVariable(destExpr->variable->name) : nullptr;
						if (var && var->type.canRefineTo(valType)) {
							var->type = valType;
							changed = true;
						}
					} else if (destExpr->kind == Expression::Kind::IntrinsicCall && destExpr->intrinsicName == "property" &&
							   valType.isDeduced()) {
						Type instType = resolveTypeThroughMacro(destExpr->arguments[1], macroBindings);
						if (instType.kind == Type::Kind::Class && instType.classDefinition && instType.classInstIndex >= 0) {
							Expression *propExpr = resolveVarThroughMacro(destExpr->arguments[2], macroBindings);
							std::string fieldName;
							if (auto *str = std::get_if<std::string>(&propExpr->literalValue))
								fieldName = *str;
							if (!fieldName.empty()) {
								ClassDefinition *classDef = instType.classDefinition;
								auto &fieldTypes = classDef->instantiations[instType.classInstIndex].fieldTypes;
								for (size_t i = 0; i < classDef->fields.size(); i++) {
									if (classDef->fields[i].name == fieldName && fieldTypes[i].canRefineTo(valType)) {
										fieldTypes[i] = valType;
										changed = true;
										break;
									}
								}
							}
						}
					}
				}
				expr->type = {Type::Kind::Void};
				break;
			case IntrinsicReturnKind::Float:
				expr->type = {Type::Kind::Float, 4};
				break;
			case IntrinsicReturnKind::Custom:
				if (expr->intrinsicName == "address of") {
					if (expr->arguments.size() >= 2) {
						Type varType = resolveTypeThroughMacro(expr->arguments[1], macroBindings);
						if (varType.isDeduced())
							expr->type = varType.pointed();
					}
				} else if (expr->intrinsicName == "dereference") {
					if (expr->arguments.size() >= 2) {
						Type ptrType = resolveTypeThroughMacro(expr->arguments[1], macroBindings);
						if (ptrType.isDeduced() && ptrType.isPointer())
							expr->type = ptrType.dereferenced();
					}
				} else if (expr->intrinsicName == "load at") {
					expr->type = {Type::Kind::Integer, 8};
				} else if (expr->intrinsicName == "return") {
					if (expr->arguments.size() >= 2) {
						Type retType = resolveTypeThroughMacro(expr->arguments[1], macroBindings);
						if (retType.isDeduced()) {
							expr->type = retType;
							if (context.currentInstantiation)
								context.currentInstantiation->returnType = retType;
						}
					}
				} else if (expr->intrinsicName == "call") {
					if (expr->arguments.size() >= 4) {
						std::string retTypeStr;
						if (auto *str = std::get_if<std::string>(&expr->arguments[3]->literalValue))
							retTypeStr = *str;
						if (!retTypeStr.empty())
							expr->type = Type::fromString(retTypeStr);
					}
				} else if (expr->intrinsicName == "cast") {
					if (expr->arguments.size() >= 3) {
						Type typeArgType = resolveTypeThroughMacro(expr->arguments[2], macroBindings);
						if (typeArgType.kind == Type::Kind::TypeReference && typeArgType.classDefinition) {
							ClassDefinition *classDef = typeArgType.classDefinition;
							int instIdx = classDef->instantiations.empty() ? -1 : 0;
							expr->type = {Type::Kind::Class, 0, 0, classDef, instIdx};
						} else {
							std::string targetStr;
							if (auto *str = std::get_if<std::string>(&expr->arguments[2]->literalValue))
								targetStr = *str;
							if (targetStr == "integer" || targetStr == "float") {
								Type::Kind kind = targetStr == "integer" ? Type::Kind::Integer : Type::Kind::Float;
								int byteSize = 8;
								if (expr->arguments.size() >= 4) {
									if (auto *bits = std::get_if<int64_t>(&expr->arguments[3]->literalValue))
										byteSize = *bits / 8;
								}
								expr->type = {kind, byteSize};
							} else if (!targetStr.empty()) {
								expr->type = Type::fromString(targetStr);
							}
						}
					}
				} else if (expr->intrinsicName == "construct") {
					if (expr->arguments.size() >= 2) {
						Type typeRefType = resolveTypeThroughMacro(expr->arguments[1], macroBindings);
						if (typeRefType.kind == Type::Kind::TypeReference && typeRefType.classDefinition) {
							ClassDefinition *classDef = typeRefType.classDefinition;
							std::vector<Type> fieldTypes;
							bool allDeduced = true;
							for (size_t i = 2; i < expr->arguments.size(); i++) {
								Type ft = resolveTypeThroughMacro(expr->arguments[i], macroBindings);
								if (!ft.isDeduced())
									allDeduced = false;
								fieldTypes.push_back(ft);
							}
							if (allDeduced) {
								int instIdx = classDef->getOrCreateInstantiation(fieldTypes);
								expr->type = {Type::Kind::Class, 0, 0, classDef, instIdx};
							}
						}
					}
				} else if (expr->intrinsicName == "property") {
					if (expr->arguments.size() >= 3) {
						Type instType = resolveTypeThroughMacro(expr->arguments[1], macroBindings);
						if (instType.kind == Type::Kind::Class && instType.classDefinition && instType.classInstIndex >= 0) {
							Expression *propExpr = resolveVarThroughMacro(expr->arguments[2], macroBindings);
							std::string fieldName;
							if (auto *str = std::get_if<std::string>(&propExpr->literalValue))
								fieldName = *str;
							if (!fieldName.empty()) {
								ClassDefinition *classDef = instType.classDefinition;
								for (size_t i = 0; i < classDef->fields.size(); i++) {
									if (classDef->fields[i].name == fieldName) {
										expr->type = classDef->instantiations[instType.classInstIndex].fieldTypes[i];
										break;
									}
								}
							}
						}
					}
				}
				break;
			}
		}
		break;
	}

	case Expression::Kind::PatternCall: {
		if (expr->patternMatch && expr->patternMatch->matchedEndNode) {
			PatternDefinition *def = expr->patternMatch->matchedEndNode->matchingDefinition;
			if (def && def->section) {
				Section *matchedSection = def->section;

				// Build parameter bindings from call-site arguments
				std::vector<Expression *> sortedArgs = sortArgumentsByPosition(expr->arguments);

				std::unordered_map<std::string, Expression *> callBindings;
				size_t argIndex = 0;
				for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
					auto paramIt = node->parameterNames.find(def);
					if (paramIt != node->parameterNames.end() && argIndex < sortedArgs.size()) {
						// Resolve through current macro bindings if we're inside a macro
						Expression *actualArg = sortedArgs[argIndex];
						if (actualArg->kind == Expression::Kind::Variable && actualArg->variable) {
							auto macroIt = macroBindings.find(actualArg->variable->name);
							if (macroIt != macroBindings.end()) {
								actualArg = macroIt->second;
							}
						}
						callBindings[paramIt->second] = actualArg;
						argIndex++;
					}
				}

				if (matchedSection->type == SectionType::Class) {
					auto *classSec = static_cast<ClassSection *>(matchedSection);
					expr->type = {Type::Kind::TypeReference, 0, 0, classSec->classDefinition};
				} else if (matchedSection->type == SectionType::Effect) {
					// Effects: infer body, result type is Void
					changed |= inferMacroBody(matchedSection, callBindings, context);
					expr->type = {Type::Kind::Void};
				} else if (matchedSection->isMacro) {
					// Code replacement: infer body, type = replacement expression type
					changed |= inferMacroBody(matchedSection, callBindings, context);
					for (Section *child : matchedSection->children) {
						for (CodeLine *line : child->codeLines) {
							if (line->expression && line->expression->type.isDeduced())
								expr->type = line->expression->type;
						}
					}
				} else {
					// Non-macro function: infer body per-instantiation
					// Build argTypes in nodesPassed order (must match codegen's paramBindings order)
					std::vector<Type> argTypes;
					size_t argTypeIndex = 0;
					for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
						auto paramIt = node->parameterNames.find(def);
						if (paramIt != node->parameterNames.end() && argTypeIndex < sortedArgs.size()) {
							Expression *argExpr = callBindings[paramIt->second];
							argTypes.push_back(resolveTypeThroughMacro(argExpr, macroBindings));
							argTypeIndex++;
						}
					}

					// Skip if any argument type is undeduced — can't meaningfully
					// infer the body without knowing all argument types.
					bool allDeduced = true;
					for (auto &t : argTypes)
						if (!t.isDeduced()) {
							allDeduced = false;
							break;
						}
					if (!allDeduced)
						break;

					Instantiation &inst = matchedSection->instantiations[argTypes];
					if (!inst.inferring) {
						inst.inferring = true;
						Instantiation *savedInst = context.currentInstantiation;
						context.currentInstantiation = &inst;
						changed |= inferMacroBody(matchedSection, callBindings, context);
						context.currentInstantiation = savedInst;
						inst.inferring = false;
					}

					if (inst.returnType.isDeduced())
						expr->type = inst.returnType;
				}
			}
		}
		break;
	}

	case Expression::Kind::Pending:
		break;
	}

	return changed || (expr->type != oldType);
}

// Reset non-literal expression types in a section and all its children.
// Macro body expression nodes are shared across all callers. Without resetting,
// if a previous caller set the body type to (e.g.) f64, and the current caller
// has an undeduced operand, the arithmetic guard skips the update and the stale
// f64 type is read as if it were the current call's result.
static void resetExpressionTypes(Expression *expr) {
	if (!expr)
		return;
	if (expr->kind != Expression::Kind::Literal)
		expr->type = {};
	for (Expression *arg : expr->arguments)
		resetExpressionTypes(arg);
}

static void resetSectionTypes(Section *section) {
	for (CodeLine *line : section->codeLines)
		if (line->expression)
			resetExpressionTypes(line->expression);
	for (Section *child : section->children)
		resetSectionTypes(child);
}

static bool
inferMacroBody(Section *section, const std::unordered_map<std::string, Expression *> &bindings, ParseContext &context) {
	// Only macros need resetting — non-macro functions are inferred per-
	// instantiation and their body types must persist across iterations.
	if (section->isMacro)
		resetSectionTypes(section);

	bool changed = false;
	for (CodeLine *line : section->codeLines) {
		if (line->expression)
			changed |= inferExpressionType(line->expression, context, bindings);
	}
	for (Section *child : section->children)
		changed |= inferMacroBody(child, bindings, context);
	return changed;
}

// Default a Numeric expression to a sized Integer type.
// For literals, check if the value fits in i32; otherwise use i64.
// For non-literal Numeric expressions, default to i32.
static void defaultNumericExpressions(Expression *expr) {
	if (!expr)
		return;
	if (expr->type.kind == Type::Kind::Numeric) {
		int size = 4; // default to i32
		if (expr->kind == Expression::Kind::Literal) {
			if (auto *intVal = std::get_if<int64_t>(&expr->literalValue)) {
				if (*intVal < INT32_MIN || *intVal > INT32_MAX)
					size = 8;
			}
		}
		expr->type = {Type::Kind::Integer, size};
	}
	for (Expression *arg : expr->arguments)
		defaultNumericExpressions(arg);
}

static void defaultNumericTypes(Section *section) {
	for (auto &[name, var] : section->variables) {
		if (var->type.kind == Type::Kind::Numeric)
			var->type = {Type::Kind::Integer, 4}; // default to i32
	}
	// Default Numeric→Integer(4) in class instantiation field types
	if (section->type == SectionType::Class) {
		auto *classSec = static_cast<ClassSection *>(section);
		for (ClassInstantiation &inst : classSec->classDefinition->instantiations) {
			for (Type &ft : inst.fieldTypes) {
				if (ft.kind == Type::Kind::Numeric)
					ft = {Type::Kind::Integer, 4};
			}
		}
	}
	// Default Numeric→Integer(4) in instantiation map keys
	if (!section->instantiations.empty()) {
		std::map<std::vector<Type>, Instantiation> updated;
		for (auto &[argTypes, inst] : section->instantiations) {
			std::vector<Type> defaultedTypes = argTypes;
			for (Type &t : defaultedTypes) {
				if (t.kind == Type::Kind::Numeric)
					t = {Type::Kind::Integer, 4};
			}
			if (inst.returnType.kind == Type::Kind::Numeric)
				inst.returnType = {Type::Kind::Integer, 4};
			updated[defaultedTypes] = std::move(inst);
		}
		section->instantiations = std::move(updated);
	}
	for (Section *child : section->children)
		defaultNumericTypes(child);
}

// Validate types after inference — check for type errors
static bool validateExpressionTypes(Expression *expr, ParseContext &context) {
	if (!expr)
		return true;

	bool valid = true;
	for (Expression *arg : expr->arguments)
		valid &= validateExpressionTypes(arg, context);

	if (expr->kind == Expression::Kind::IntrinsicCall) {
		if (isArithmeticOperator(expr->intrinsicName)) {
			if (expr->arguments.size() >= 3) {
				Type leftType = expr->arguments[1]->type;
				Type rightType = expr->arguments[2]->type;
				// Pointer arithmetic (ptr + int, ptr - int) is valid
				bool ptrArith =
					isPointerArithmeticOperator(expr->intrinsicName) && (leftType.isPointer() || rightType.isPointer());
				if (!ptrArith && leftType.isDeduced() && !leftType.isNumeric()) {
					context.diagnostics.push_back(Diagnostic(
						Diagnostic::Level::Error,
						"Cannot use " + leftType.toString() + " in arithmetic (expected a numeric type)",
						expr->arguments[1]->range
					));
					valid = false;
				}
				if (!ptrArith && rightType.isDeduced() && !rightType.isNumeric()) {
					context.diagnostics.push_back(Diagnostic(
						Diagnostic::Level::Error,
						"Cannot use " + rightType.toString() + " in arithmetic (expected a numeric type)",
						expr->arguments[2]->range
					));
					valid = false;
				}
			}
		} else if (isComparisonOperator(expr->intrinsicName)) {
			if (expr->arguments.size() >= 3) {
				Type leftType = expr->arguments[1]->type;
				Type rightType = expr->arguments[2]->type;
				if (leftType.isDeduced() && rightType.isDeduced() && !leftType.isNumeric() && !rightType.isNumeric() &&
					leftType != rightType) {
					context.diagnostics.push_back(Diagnostic(
						Diagnostic::Level::Error, "Cannot compare " + leftType.toString() + " with " + rightType.toString(),
						expr->range
					));
					valid = false;
				}
			}
		} else if (expr->intrinsicName == "negate") {
			if (expr->arguments.size() >= 2) {
				Type operandType = expr->arguments[1]->type;
				if (operandType.isDeduced() && !operandType.isNumeric()) {
					context.diagnostics.push_back(Diagnostic(
						Diagnostic::Level::Error, "Cannot negate " + operandType.toString() + " (expected a numeric type)",
						expr->arguments[1]->range
					));
					valid = false;
				}
			}
		}
	}

	return valid;
}

bool inferTypes(ParseContext &context) {
	// Type inference uses fixed-point iteration: types flow through expressions
	// until no more changes occur. 64 iterations handles deeply nested expressions
	// with complex type dependencies (macros, pattern calls, arithmetic promotion).
	for (int iteration = 0; iteration < 64; iteration++) {
		bool changed = false;

		for (CodeLine *line : context.codeLines) {
			if (line->expression) {
				changed |= inferExpressionType(line->expression, context);
			}
		}

		if (!changed)
			break;
	}

	// Default remaining Numeric types to sized Integer
	for (CodeLine *line : context.codeLines) {
		if (line->expression)
			defaultNumericExpressions(line->expression);
	}
	defaultNumericTypes(context.mainSection);

	// Validate variables — all must have deduced types
	// Skip non-macro function body sections: their variables only get types during monomorphization
	bool valid = true;
	std::function<void(Section *)> validateVariables = [&](Section *section) {
		if (section->parent && !section->parent->isMacro && !section->parent->patternDefinitions.empty())
			return;
		for (auto &[name, var] : section->variables) {
			if (!var->type.isDeduced()) {
				context.diagnostics.push_back(Diagnostic(
					Diagnostic::Level::Error, "Variable '" + name + "' has no type (never assigned a value)",
					var->definition->range
				));
				valid = false;
			}
		}
		for (Section *child : section->children)
			validateVariables(child);
	};
	validateVariables(context.mainSection);

	// Validate non-macro expression functions have deduced return types
	std::function<void(Section *)> validateReturnTypes = [&](Section *section) {
		if (section->type == SectionType::Expression && !section->isMacro && !section->patternDefinitions.empty()) {
			for (auto &[argTypes, inst] : section->instantiations) {
				if (!inst.returnType.isDeduced()) {
					context.diagnostics.push_back(Diagnostic(
						Diagnostic::Level::Error,
						"Expression '" + (std::string)section->patternDefinitions.front()->range.subString +
							"' has no deduced return type",
						section->patternDefinitions.front()->range
					));
					valid = false;
					break; // one error per section is enough
				}
			}
		}
		for (Section *child : section->children)
			validateReturnTypes(child);
	};
	validateReturnTypes(context.mainSection);

	// Validate expression types
	for (CodeLine *line : context.codeLines) {
		if (line->expression)
			valid &= validateExpressionTypes(line->expression, context);
	}

	return valid;
}

bool isArithmeticOperator(const std::string &name) {
	return name == "add" || name == "subtract" || name == "multiply" || name == "divide" || name == "modulo";
}

bool isPointerArithmeticOperator(const std::string &name) { return name == "add" || name == "subtract"; }

bool isComparisonOperator(const std::string &name) {
	const IntrinsicInfo *info = findIntrinsic(name);
	return info && info->returnKind == IntrinsicReturnKind::Bool && name != "and" && name != "or" && name != "not";
}

bool isMathFunction(const std::string &name) {
	const IntrinsicInfo *info = findIntrinsic(name);
	return info && info->returnKind == IntrinsicReturnKind::SameAsArgs && !isArithmeticOperator(name) && name != "negate";
}
