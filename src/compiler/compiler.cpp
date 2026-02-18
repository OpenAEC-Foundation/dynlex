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
static std::string resolveImportPath(const std::string &path, lsp::FileSystem *fileSystem) {
	// Try the path as-is first (relative to CWD)
	if (fileSystem->getFile(path)) {
		return path;
	}

	// Try installed system path
	std::string systemPath = "/usr/share/dynlex/" + path;
	if (fileSystem->getFile(systemPath)) {
		return systemPath;
	}

	// Try relative to the project source directory (for development builds)
	std::string devPath = std::string(PROJECT_SOURCE_DIR) + "/" + path;
	if (fileSystem->getFile(devPath)) {
		return devPath;
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
			std::string importPath =
				resolveImportPath(std::string(line->rightTrimmedText.substr("import "sv.length())), context.fileSystem.get());
			if (!importSourceFile(importPath, context)) {
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
