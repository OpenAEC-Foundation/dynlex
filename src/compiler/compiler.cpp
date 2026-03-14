#include "compiler.h"
#include "IndentData.h"
#include "classSection.h"
#include "compileTimeValue.h"
#include "function.h"
#include "intrinsicInfo.h"
#include "lsp/fileSystem.h"
#include "lsp/sourceFile.h"
#include "pathUtils.h"
#include "pattern/pattern_tree/patternElement.h"
#include "pattern/pattern_tree/patternTreeNode.h"
#include "stringFunctions.h"
#include "syntaxConfig.h"
#include "type.h"
#include <cctype>
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

// regex for line terminators - matches each line including its terminator
const std::regex lineWithTerminatorRegex("([^\r\n]*(?:\r\n|\r|\n))|([^\r\n]+$)");

static void updateLineTrimming(CodeLine *line, const SyntaxConfig &syntax) {
	size_t commentPos = findCommentStart(line->fullText, syntax.commentPrefix);
	std::string_view withoutComment =
		(commentPos != std::string_view::npos) ? line->fullText.substr(0, commentPos) : line->fullText;
	std::cmatch match;
	std::regex_search(withoutComment.begin(), withoutComment.end(), match, std::regex("[\\s]+$"));
	line->rightTrimmedText = match.empty() ? withoutComment : withoutComment.substr(0, match.position());
}

static CodeLine *createMappedLine(
	ParseContext &context, lsp::SourceFile *primarySourceFile, int primarySourceLineIndex, std::string text,
	std::vector<SourceSlice> sourceSlices
) {
	auto line = std::make_unique<CodeLine>(std::string_view{}, primarySourceFile);
	line->sourceFile = primarySourceFile;
	line->sourceFileLineIndex = primarySourceLineIndex;
	line->setOwnedText(std::move(text));
	line->sourceSlices = std::move(sourceSlices);
	CodeLine *result = line.get();
	context.ownedCodeLines.push_back(std::move(line));
	return result;
}

static CodeLine *
createSourceLine(ParseContext &context, lsp::SourceFile *sourceFile, int sourceFileLineIndex, std::string_view text) {
	CodeLine *line = createMappedLine(
		context, sourceFile, sourceFileLineIndex, std::string(text),
		{{0, static_cast<int>(text.size()), sourceFile, sourceFileLineIndex, 0}}
	);
	const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, sourceFile);
	updateLineTrimming(line, syntax);
	return line;
}

struct BracketContinuationState {
	int parentheses = 0;
	int brackets = 0;
	bool inString = false;
	bool escaped = false;
};

static void advanceContinuationState(std::string_view text, BracketContinuationState &state) {
	for (char ch : text) {
		if (state.inString) {
			if (state.escaped) {
				state.escaped = false;
				continue;
			}
			if (ch == '\\') {
				state.escaped = true;
			} else if (ch == '"') {
				state.inString = false;
			}
			continue;
		}
		if (ch == '"') {
			state.inString = true;
		} else if (ch == '(') {
			state.parentheses++;
		} else if (ch == ')' && state.parentheses > 0) {
			state.parentheses--;
		} else if (ch == '[') {
			state.brackets++;
		} else if (ch == ']' && state.brackets > 0) {
			state.brackets--;
		}
	}
}

static bool needsContinuation(const BracketContinuationState &state) {
	return state.inString || state.parentheses > 0 || state.brackets > 0;
}

static std::string_view trimLeadingWhitespace(std::string_view text) {
	size_t first = text.find_first_not_of(" \t");
	return first == std::string_view::npos ? std::string_view{} : text.substr(first);
}

static CodeLine *mergeContinuationLines(ParseContext &context, const std::vector<CodeLine *> &lines) {
	if (lines.empty())
		return nullptr;
	if (lines.size() == 1)
		return lines.front();

	std::string mergedText;
	std::vector<SourceSlice> slices;
	for (size_t i = 0; i < lines.size(); i++) {
		std::string_view piece = lines[i]->rightTrimmedText;
		int sourceColumnStart = 0;
		if (i > 0) {
			piece = trimLeadingWhitespace(piece);
			sourceColumnStart = static_cast<int>(piece.begin() - lines[i]->fullText.begin());
			if (!mergedText.empty() && !std::isspace(static_cast<unsigned char>(mergedText.back())) && !piece.empty()) {
				mergedText += ' ';
			}
		}
		if (i == 0)
			sourceColumnStart = static_cast<int>(piece.begin() - lines[i]->fullText.begin());
		int transformedStart = static_cast<int>(mergedText.size());
		mergedText += piece;
		slices.push_back({
			transformedStart,
			static_cast<int>(mergedText.size()),
			lines[i]->sourceFile,
			lines[i]->sourceFileLineIndex,
			sourceColumnStart,
		});
	}

	CodeLine *merged = createMappedLine(
		context, lines.front()->sourceFile, lines.front()->sourceFileLineIndex, std::move(mergedText), std::move(slices)
	);
	merged->rightTrimmedText = merged->fullText;
	return merged;
}

static void applyBracketContinuations(ParseContext &context) {
	std::vector<CodeLine *> transformedLines;
	for (size_t i = 0; i < context.codeLines.size(); i++) {
		CodeLine *line = context.codeLines[i];
		if (line->rightTrimmedText.empty()) {
			transformedLines.push_back(line);
			continue;
		}

		BracketContinuationState state;
		advanceContinuationState(line->rightTrimmedText, state);
		if (!needsContinuation(state)) {
			transformedLines.push_back(line);
			continue;
		}

		std::vector<CodeLine *> mergedLines = {line};
		while (needsContinuation(state) && i + 1 < context.codeLines.size()) {
			CodeLine *next = context.codeLines[++i];
			mergedLines.push_back(next);
			advanceContinuationState(next->rightTrimmedText, state);
		}
		transformedLines.push_back(mergeContinuationLines(context, mergedLines));
	}
	context.codeLines = std::move(transformedLines);
}

static std::string canonicalSourceKey(std::string_view path) {
	std::filesystem::path fsPath = pathutil::toFilesystemPath(path);
	std::error_code ec;
	std::filesystem::path canonical = std::filesystem::weakly_canonical(fsPath, ec);
	if (ec) {
		canonical = std::filesystem::absolute(fsPath, ec);
		if (ec)
			return fsPath.lexically_normal().string();
	}
	return canonical.lexically_normal().string();
}

bool isInternalSourcePath(std::string_view path) {
	if (path.empty())
		return false;

	std::string normalized = canonicalSourceKey(path);
	std::replace(normalized.begin(), normalized.end(), '\\', '/');

	const std::string projectLibPrefix = std::string(PROJECT_SOURCE_DIR) + "/lib/";
	return normalized.starts_with("lib/") || normalized.starts_with("/usr/share/dynlex/lib/") ||
		   normalized.starts_with(projectLibPrefix);
}

PatternDefinition *findDefinitionBySignature(ParseContext &context, SectionType sectionType, std::string_view signature) {
	std::string converted(signature);
	for (char &c : converted) {
		if (c == '$')
			c = argumentChar;
	}

	auto elements = getPatternElements(converted);
	PatternTreeNode *node = context.patternTrees[(int)sectionType];
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

	return (node && !node->matchingDefinitions.empty()) ? node->matchingDefinitions[0] : nullptr;
}

static DataType concretizeClassType(DataType type) {
	if (type.kind == DataType::Kind::Class && type.classDefinition && type.classInstIndex < 0 &&
		!type.classDefinition->instantiations.empty()) {
		type.classInstIndex = 0;
	}
	return type;
}

static bool evaluateCompileTimeInteger(ParseContext &context, Function *expr, const BindingMap &bindings, int &outValue) {
	CompileTimeValue value = evaluateCompileTimeValue(expr, context, makeBindingFrameStack(bindings));
	auto *number = std::get_if<double>(&value);
	if (!number)
		return false;
	outValue = static_cast<int>(*number);
	return *number == static_cast<double>(outValue);
}

void appendPatternCallBindings(Function *expr, PatternDefinition *definition, BindingMap &bindings) {
	collectPatternCallBindings(expr, definition, bindings);
}

static bool tryParseIntrinsicTypeReference(Function *intrinsicExpr, DataType &outTypeRef) {
	if (!intrinsicExpr || intrinsicKind(intrinsicExpr->intrinsicName) != IntrinsicKind::Type ||
		intrinsicExpr->arguments.size() < 2)
		return false;

	Function *kindExpr = intrinsicExpr->arguments[1];
	auto *kindStr = std::get_if<std::string>(&kindExpr->literalValue);
	if (!kindStr)
		return false;

	DataType typeRef;
	typeRef.kind = DataType::Kind::Type;
	if (*kindStr == "int") {
		typeRef.referencedKind = DataType::Kind::Int;
		typeRef.numericSize = 4;
	} else if (*kindStr == "float") {
		typeRef.referencedKind = DataType::Kind::Float;
		typeRef.numericSize = 8;
	} else if (*kindStr == "bool") {
		typeRef.referencedKind = DataType::Kind::Bool;
	} else if (*kindStr == "void") {
		typeRef.referencedKind = DataType::Kind::Void;
	} else if (*kindStr == "string") {
		typeRef.referencedKind = DataType::Kind::Int;
		typeRef.numericSize = 1;
		typeRef.pointerDepth = 1;
	} else if (*kindStr == "type") {
		typeRef.referencedKind = DataType::Kind::Type;
	} else {
		return false;
	}

	if (intrinsicExpr->arguments.size() > 2) {
		Function *bitsExpr = intrinsicExpr->arguments[2];
		auto *bits = std::get_if<double>(&bitsExpr->literalValue);
		if (!bits)
			return false;
		typeRef.numericSize = (int)*bits / 8;
	}

	outTypeRef = typeRef;
	return true;
}

static bool
resolveTypeReferenceFunction(ParseContext &context, Function *expr, const BindingMap &bindings, DataType &outTypeRef);

static bool instantiateClassTypeReference(
	ParseContext &context, ClassDefinition *classDef, const BindingMap &bindings, DataType &outTypeRef
) {
	if (!classDef)
		return false;

	std::vector<DataType> fieldTypes;
	fieldTypes.reserve(classDef->fields.size());
	for (FieldDefinition &field : classDef->fields) {
		DataType fieldType = field.declaredType;
		if (fieldType.kind == DataType::Kind::Any) {
			outTypeRef = {DataType::Kind::Type, 0, 0, classDef, -1, nullptr, DataType::Kind::Class};
			return true;
		}
		if (fieldType.kind == DataType::Kind::Unresolved && fieldType.typeFunction) {
			if (fieldType.typeFunction->kind == Function::Kind::Pending && field.range.line && field.range.line->section)
				expandFunction(fieldType.typeFunction, field.range.line->section);

			DataType fieldTypeRef;
			if (!resolveTypeReferenceFunction(context, fieldType.typeFunction, bindings, fieldTypeRef) ||
				fieldTypeRef.kind != DataType::Kind::Type)
				return false;
			fieldType = concretizeClassType(fieldTypeRef.toReferencedType());
		} else if (fieldType.kind == DataType::Kind::Class && fieldType.classInstIndex < 0) {
			fieldType = concretizeClassType(fieldType);
		}

		if (!fieldType.isDeduced()) {
			outTypeRef = {DataType::Kind::Type, 0, 0, classDef, -1, nullptr, DataType::Kind::Class};
			return true;
		}
		fieldTypes.push_back(fieldType);
	}

	int instIndex = classDef->getOrCreateInstantiation(fieldTypes);
	outTypeRef = {DataType::Kind::Type, 0, 0, classDef, instIndex, nullptr, DataType::Kind::Class};
	return true;
}

static bool
resolveTypeReferenceFunction(ParseContext &context, Function *expr, const BindingMap &bindings, DataType &outTypeRef) {
	if (!expr)
		return false;

	if (expr->kind == Function::Kind::Variable && expr->variable) {
		auto it = bindings.find(expr->variable->name);
		if (it != bindings.end())
			return resolveTypeReferenceFunction(context, it->second, bindings, outTypeRef);
		if (expr->variable->name == "pointer") {
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Int;
			outTypeRef.numericSize = 1;
			outTypeRef.pointerDepth = 1;
			return true;
		}
		DataType shorthandType = DataType::fromString(expr->variable->name);
		if (shorthandType.isDeduced()) {
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = shorthandType.kind;
			outTypeRef.numericSize = shorthandType.numericSize;
			outTypeRef.pointerDepth = shorthandType.pointerDepth;
			return true;
		}
		return false;
	}

	if (expr->kind == Function::Kind::IntrinsicCall) {
		IntrinsicKind kind = intrinsicKind(expr->intrinsicName);
		if (tryParseIntrinsicTypeReference(expr, outTypeRef))
			return true;
		if (kind == IntrinsicKind::AddPointerDepth) {
			DataType innerTypeRef;
			if (!resolveTypeReferenceFunction(context, expr->arguments[1], bindings, innerTypeRef) ||
				innerTypeRef.kind != DataType::Kind::Type)
				return false;
			innerTypeRef.pointerDepth++;
			outTypeRef = innerTypeRef;
			return true;
		}
		if (kind == IntrinsicKind::Array) {
			int arraySize = 0;
			if (!evaluateCompileTimeInteger(context, expr->arguments[1], bindings, arraySize))
				return false;
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Array;
			outTypeRef.arraySize = arraySize;
			if (expr->arguments.size() > 2) {
				DataType elementTypeRef;
				if (!resolveTypeReferenceFunction(context, expr->arguments[2], bindings, elementTypeRef) ||
					elementTypeRef.kind != DataType::Kind::Type)
					return false;
				outTypeRef.arrayElementType = std::make_shared<DataType>(elementTypeRef.toReferencedType());
			}
			return true;
		}
		return false;
	}

	if (expr->kind != Function::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return false;

	auto &defs = expr->patternMatch->matchedEndNode->matchingDefinitions;
	if (defs.empty())
		return false;

	PatternDefinition *def = defs.front();
	if (!def || !def->section)
		return false;

	if (!def->section->isMacro && def->section->type == SectionType::Class) {
		auto *classSec = static_cast<ClassSection *>(def->section);
		BindingMap classBindings = bindings;
		appendPatternCallBindings(expr, def, classBindings);
		return instantiateClassTypeReference(context, classSec->classDefinition, classBindings, outTypeRef);
	}

	BindingMap innerBindings;
	Function *bodyExpr = expandMacroPatternCall(expr, innerBindings);
	if (!bodyExpr)
		return false;

	BindingMap mergedBindings = bindings;
	for (const auto &[name, argExpr] : innerBindings)
		mergedBindings[name] = argExpr;
	return resolveTypeReferenceFunction(context, bodyExpr, mergedBindings, outTypeRef);
}

static bool resolveDeclaredClassFieldTypes(ParseContext &context) {
	std::vector<ClassDefinition *> classDefinitions;
	std::function<void(Section *)> collectClasses = [&](Section *section) {
		if (section->type == SectionType::Class) {
			auto *classSec = static_cast<ClassSection *>(section);
			classDefinitions.push_back(classSec->classDefinition);
		}
		for (Section *child : section->children)
			collectClasses(child);
	};
	collectClasses(context.mainSection);

	bool madeProgress = true;
	for (int iteration = 0; iteration < context.options.maxResolutionIterations && madeProgress; iteration++) {
		madeProgress = false;

		for (ClassDefinition *classDef : classDefinitions) {
			for (FieldDefinition &field : classDef->fields) {
				if (field.declaredType.kind == DataType::Kind::Unresolved && field.declaredType.typeFunction) {
					if (field.declaredType.typeFunction->kind == Function::Kind::Pending && field.range.line &&
						field.range.line->section) {
						expandFunction(field.declaredType.typeFunction, field.range.line->section);
					}
					DataType typeRef;
					if (resolveTypeReferenceFunction(context, field.declaredType.typeFunction, {}, typeRef) &&
						typeRef.kind == DataType::Kind::Type) {
						field.declaredType = concretizeClassType(typeRef.toReferencedType());
						madeProgress = true;
					}
				} else if (field.declaredType.kind == DataType::Kind::Class && field.declaredType.classInstIndex < 0) {
					DataType concretized = concretizeClassType(field.declaredType);
					if (concretized != field.declaredType) {
						field.declaredType = concretized;
						madeProgress = true;
					}
				}
			}
		}

		for (ClassDefinition *classDef : classDefinitions) {
			if (classDef->fields.empty() || !classDef->instantiations.empty())
				continue;

			bool allDeclared = true;
			std::vector<DataType> fieldTypes;
			for (const FieldDefinition &field : classDef->fields) {
				if (!field.declaredType.isDeduced()) {
					allDeclared = false;
					break;
				}
				fieldTypes.push_back(field.declaredType);
			}
			if (allDeclared) {
				classDef->instantiations.push_back({fieldTypes});
				madeProgress = true;
			}
		}
	}

	return true;
}

bool compile(const std::string &path, ParseContext &context) {
	context.compilationStage = ParseContext::CompilationStage::NotStarted;
	if (!initializeSyntaxConfigs(context, path))
		return false;

	if (!importSourceFile(path, context))
		return false;
	applyBracketContinuations(context);
	context.compilationStage = ParseContext::CompilationStage::ImportedFiles;

	if (!analyzeSections(context))
		return false;
	context.compilationStage = ParseContext::CompilationStage::AnalyzedSections;

	if (!resolvePatterns(context))
		return false;
	context.compilationStage = ParseContext::CompilationStage::ResolvedPatterns;

	if (!resolveDeclaredClassFieldTypes(context))
		return false;
	context.compilationStage = ParseContext::CompilationStage::ResolvedDeclaredTypes;

	if (!validate(context))
		return false;
	context.compilationStage = ParseContext::CompilationStage::Validated;

	if (!inferTypes(context))
		return false;
	context.compilationStage = ParseContext::CompilationStage::InferredTypes;

	return true;
}

bool importSourceFile(const std::string &path, ParseContext &context) {
	lsp::SourceFile *sourceFile = context.fileSystem->getFile(path);
	if (!sourceFile) {
		if (context.importedFiles.empty()) {
			// If this is the main file, report error
			context.diagnostics.push_back(Diagnostic(
				Diagnostic::Level::Error,
				renderSyntaxMessage(context.projectSyntax.messages.couldNotImportMainFile, {{"path", path}}), Range()
			));
		}
		return false;
	}

	// Canonicalize path to prevent duplicate imports via different path strings
	std::string canonicalPath = canonicalSourceKey(sourceFile->uri.empty() ? path : sourceFile->uri);
	if (context.importedFiles.contains(canonicalPath)) {
		return true; // Already processed, skip
	}

	context.importedFiles[canonicalPath] = sourceFile;
	// The first file imported is the main source file
	if (!context.mainSourceFile)
		context.mainSourceFile = sourceFile;

	// iterate over lines, each match includes the line terminator
	std::string_view fileView{sourceFile->content};
	const SyntaxConfig &syntax = syntaxConfigForSourcePath(context, sourceFile->uri.empty() ? path : sourceFile->uri);

	std::cregex_iterator iter(fileView.begin(), fileView.end(), lineWithTerminatorRegex);
	std::cregex_iterator end;
	int sourceFileLineIndex = 0;
	for (; iter != end; ++iter, ++sourceFileLineIndex) {
		std::string_view lineString = fileView.substr(iter->position(), iter->length());
		CodeLine *line = createSourceLine(context, sourceFile, sourceFileLineIndex, lineString);

		// check if the line is an import statement
		if (std::optional<std::string_view> importPathView =
				extractDirectiveArgument(line->rightTrimmedText, syntax.importKeyword)) {
			// recursively import the file, replacing this line with the imported content
			std::string importingDir =
				std::filesystem::path(pathutil::toFilesystemPath(sourceFile->uri.empty() ? path : sourceFile->uri))
					.parent_path()
					.string();
			std::string importPath = resolveImportPath(std::string(*importPathView), importingDir, context.fileSystem.get());
			if (!importSourceFile(importPath, context)) {
				context.diagnostics.push_back(Diagnostic(
					Diagnostic::Level::Error,
					renderSyntaxMessage(syntax.messages.failedToImportSourceFile, {{"path", importPath}}),
					Range(line, static_cast<int>(syntax.importKeyword.length()), line->rightTrimmedText.length())
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
				renderSyntaxMessage(
					syntax.messages.invalidIndentationAmount,
					{
						{"expected", std::to_string(data.indentString.length() * data.indentLevel) + " " +
										 charName(data.indentString[0]) + "s"},
						{"found", std::to_string(indentString.length())},
					}
				),
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
					renderSyntaxMessage(
						syntax.messages.invalidIndentationCharacter,
						{{"expected", charName(expectedIndentChar) + "s"},
						 {"found", "a " + charName(indentString[invalidCharIndex])}}
					),
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
					renderSyntaxMessage(
						syntax.messages.invalidIndentationIncrease,
						{
							{"expected", std::to_string(data.indentString.length() * oldIndentLevel) + " " +
											 charName(data.indentString[0]) + "s"},
							{"found", std::to_string(indentString.length())},
						}
					),
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
		if (trimmedText.ends_with(syntax.sectionOpener)) {
			line->patternText = trimmedText.substr(0, trimmedText.length() - syntax.sectionOpener.length());

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
			if (extractDirectiveArgument(line->patternText, syntax.importKeyword)) {
				// Import lines are already processed during importSourceFile;
				// tokenize "import" as a keyword and the path as a string
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

bool isArithmeticOperator(const std::string &name) { return isArithmeticIntrinsic(arithmeticIntrinsicKind(name)); }

bool isPointerArithmeticOperator(const std::string &name) {
	return isPointerArithmeticIntrinsic(arithmeticIntrinsicKind(name));
}

bool isComparisonOperator(const std::string &name) { return isComparisonIntrinsicKind(intrinsicKind(name)); }

bool isMathFunction(const std::string &name) {
	const IntrinsicInfo *info = findIntrinsic(name);
	return info && info->returnKind == IntrinsicReturnKind::SameAsArgs && !isArithmeticOperator(name) &&
		   intrinsicKind(name) != IntrinsicKind::Negate;
}

PatternDefinition *selectOverload(
	const std::vector<PatternDefinition *> &definitions, const std::vector<Function *> & /*sortedArgs*/,
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
						if (!argType.isDeduced()) {
							// Keep candidate viable until the argument type is known.
							break;
						}
						bool matches = false;
						if (constraint.kind == DataType::Kind::Class) {
							matches =
								argType.kind == DataType::Kind::Class && argType.classDefinition == constraint.classDefinition;
						} else if (constraint.kind == DataType::Kind::Type) {
							// {type:x} accepts any compile-time type value, including pointer/array/etc. type refs.
							matches = argType.kind == DataType::Kind::Type;
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
