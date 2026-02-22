#include "compiler.h"
#include "IndentData.h"
#include "classSection.h"
#include "expression.h"
#include "intrinsicInfo.h"
#include "lsp/fileSystem.h"
#include "lsp/sourceFile.h"
#include "stringFunctions.h"
#include "type.h"
#include <filesystem>
#include <regex>
using namespace std::literals;

// Search paths for library imports (e.g., "lib/std.dl")
// Tries: original path, then installed location, then source tree
static std::string
resolveImportPath(const std::string &path, const std::string &importingFileDir, lsp::FileSystem *fileSystem) {
	// Try relative to the importing file's directory first
	if (!importingFileDir.empty()) {
		std::string relativePath = importingFileDir + "/" + path;
		if (fileSystem->getFile(relativePath)) {
			return relativePath;
		}
	}

	// Try the path as-is (relative to CWD)
	if (fileSystem->getFile(path)) {
		return path;
	}

	// Try relative to the project source directory (for development builds)
	// These come before system paths so dev builds use the source tree's libraries
	std::string devPath = std::string(PROJECT_SOURCE_DIR) + "/" + path;
	if (fileSystem->getFile(devPath)) {
		return devPath;
	}

	// Try project lib directory (e.g., "std.dl" → "<project>/lib/std.dl")
	std::string devLibPath = std::string(PROJECT_SOURCE_DIR) + "/lib/" + path;
	if (fileSystem->getFile(devLibPath)) {
		return devLibPath;
	}

	// Try installed system path
	std::string systemPath = "/usr/share/dynlex/" + path;
	if (fileSystem->getFile(systemPath)) {
		return systemPath;
	}

	// Try installed library path (e.g., "std.dl" → "/usr/share/dynlex/lib/std.dl")
	std::string systemLibPath = "/usr/share/dynlex/lib/" + path;
	if (fileSystem->getFile(systemLibPath)) {
		return systemLibPath;
	}

	return path; // Return original path (will fail with proper error)
}

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
	return importSourceFile(path, context) && analyzeSections(context) && resolvePatterns(context) && validate(context) &&
		   inferTypes(context);
}

bool importSourceFile(const std::string &path, ParseContext &context) {
	lsp::SourceFile *sourceFile = context.fileSystem->getFile(path);
	if (!sourceFile) {
		if (context.importedFiles.empty()) {
			// If this is the main file, report error
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "couldn't import main file: " + path, Range()));
		}
		return false;
	}

	// Canonicalize path to prevent duplicate imports via different path strings
	auto canonicalPath = std::filesystem::weakly_canonical(path).string();
	if (context.importedFiles.contains(canonicalPath)) {
		return true; // Already processed, skip
	}

	context.importedFiles[canonicalPath] = sourceFile;
	// The first file imported is the main source file
	if (!context.mainSourceFile)
		context.mainSourceFile = sourceFile;

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
			std::string importingDir = std::filesystem::path(path).parent_path().string();
			std::string importPath = resolveImportPath(
				std::string(line->rightTrimmedText.substr("import "sv.length())), importingDir, context.fileSystem.get()
			);
			if (!importSourceFile(importPath, context)) {
				context.diagnostics.push_back(Diagnostic(
					Diagnostic::Level::Error, "failed to import source file: " + (std::string)importPath,
					Range(line, "import "sv.length(), line->rightTrimmedText.length())
				));
				return false;
			}
			// Fall through to add import line to codeLines (tokenized during section analysis)
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
			currentSection->openingLine = line;
			line->sectionOpening = currentSection;
			data.indentLevel++;
		} else {
			line->patternText = trimmedText;
			if (line->patternText.starts_with("import ")) {
				// Import lines are already processed during importSourceFile;
				// tokenize "import" as effect and the path as string
				line->resolved = true;
			} else if (line->patternText.length()) {
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
				std::vector<DataType> fieldTypes;
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

PatternDefinition *selectOverload(
	const std::vector<PatternDefinition *> &definitions, const std::vector<Expression *> & /*sortedArgs*/,
	const std::vector<PatternTreeNode *> &nodesPassed, const std::vector<DataType> &argTypes
) {
	if (definitions.size() <= 1)
		return definitions.empty() ? nullptr : definitions[0];

	// Score each candidate: count how many type constraints match
	PatternDefinition *best = nullptr;
	int bestScore = -1;

	for (auto *candidate : definitions) {
		int score = 0;
		bool constraintFailed = false;

		// Walk nodesPassed to map arguments to parameters for this candidate
		size_t argIdx = 0;
		for (PatternTreeNode *node : nodesPassed) {
			auto paramIt = node->parameterNames.find(candidate);
			if (paramIt == node->parameterNames.end() || argIdx >= argTypes.size()) {
				if (paramIt != node->parameterNames.end())
					argIdx++; // still counts as parameter slot
				continue;
			}

			// Find the corresponding element in the candidate's definition to get its type constraint
			const std::string &paramName = paramIt->second;
			for (auto &elem : candidate->patternElements) {
				if (elem.type == PatternElement::Type::Variable && elem.text == paramName) {
					if (elem.resolvedTypeConstraint.isDeduced()) {
						// DataType constraint exists — check if argument type matches
						const DataType &argType = argTypes[argIdx];
						const DataType &constraint = elem.resolvedTypeConstraint;
						bool matches = false;
						if (constraint.kind == DataType::Kind::Class) {
							matches =
								argType.kind == DataType::Kind::Class && argType.classDefinition == constraint.classDefinition;
						} else if (constraint.isNumeric()) {
							// Numeric constraint: match exact kind (Int or Float) and pointer depth
							// {integer:x} matches Int, {float:x} matches Float
							matches = argType.kind == constraint.kind && argType.pointerDepth == 0;
						} else {
							// Other primitive type constraint: match kind and pointer depth
							matches = argType.kind == constraint.kind && argType.pointerDepth == constraint.pointerDepth;
						}
						if (matches) {
							score++;
						} else {
							constraintFailed = true;
						}
					}
					break;
				}
			}
			argIdx++;
		}

		if (constraintFailed)
			continue;

		if (score > bestScore) {
			bestScore = score;
			best = candidate;
		}
	}

	return best;
}
