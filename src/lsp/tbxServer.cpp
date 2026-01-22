#include "tbxServer.h"
#include "codeLine.h"
#include "compiler.h"
#include "semanticTokenBuilder.h"
#include "sourceFile.h"
#include <algorithm>
#include <regex>

namespace lsp {

TbxServer::TbxServer(int port) : LanguageServer(port) {}

TbxServer::~TbxServer() = default;

InitializeResult TbxServer::onInitialize(const InitializeParams & /*params*/) {
	InitializeResult result;
	result.capabilities.textDocumentSync = 2; // Incremental
	result.capabilities.definitionProvider = true;
	result.capabilities.semanticTokensProvider.full = true;
	result.capabilities.semanticTokensProvider.legend.tokenTypes = getSemanticTokenTypes();
	result.capabilities.semanticTokensProvider.legend.tokenModifiers = getSemanticTokenModifiers();
	return result;
}

void TbxServer::onDidOpen(const DidOpenTextDocumentParams &params) {
	LanguageServer::onDidOpen(params);
	recompileDocument(params.textDocument.uri);
}

void TbxServer::onDidChange(const DidChangeTextDocumentParams &params) {
	LanguageServer::onDidChange(params);
	recompileDocument(params.textDocument.uri);
}

void TbxServer::onDidClose(const DidCloseTextDocumentParams &params) {
	parseContexts.erase(params.textDocument.uri);
	LanguageServer::onDidClose(params);
}

void TbxServer::recompileDocument(const std::string &uri) {
	auto docIt = documents.find(uri);
	if (docIt == documents.end()) {
		return;
	}

	const std::string &content = docIt->second->content;

	// Create new parse context
	auto context = std::make_unique<ParseContext>();

	// Create source file from content
	SourceFile *sourceFile = new SourceFile(uri, content);

	// Parse content line by line (similar to importSourceFile in compiler.cpp)
	const std::regex lineWithTerminatorRegex("([^\r\n]*(?:\r\n|\r|\n))|([^\r\n]+$)");
	std::string_view fileView{sourceFile->content};

	std::cregex_iterator iter(fileView.begin(), fileView.end(), lineWithTerminatorRegex);
	std::cregex_iterator end;
	int sourceFileLineIndex = 0;

	for (; iter != end; ++iter, ++sourceFileLineIndex) {
		std::string_view lineString = fileView.substr(iter->position(), iter->length());
		CodeLine *line = new CodeLine(lineString, sourceFile);
		line->sourceFileLineIndex = sourceFileLineIndex;

		// Remove comments and trim whitespace from the right
		std::cmatch match;
		std::regex_search(lineString.begin(), lineString.end(), match, std::regex("[\\s]*(?:#[\\S\\s]*)?$"));
		line->rightTrimmedText = lineString.substr(0, match.position());

		// Handle import statements - for now, mark as resolved
		// (Full import handling would require file system access)
		if (line->rightTrimmedText.starts_with("import ")) {
			line->resolved = true;
		}

		line->mergedLineIndex = context->codeLines.size();
		context->codeLines.push_back(line);
	}

	// Analyze sections and resolve patterns
	analyzeSections(*context);
	resolvePatterns(*context);

	// Convert diagnostics
	std::vector<Diagnostic> lspDiagnostics;
	for (const auto &diag : context->diagnostics) {
		lspDiagnostics.push_back(convertDiagnostic(diag));
	}

	// Store context and publish diagnostics
	parseContexts[uri] = std::move(context);
	publishDiagnostics(uri, lspDiagnostics);
}

Range TbxServer::convertRange(const ::Range &range) const {
	Range lspRange;
	lspRange.start.line = range.line->sourceFileLineIndex;
	lspRange.start.character = range.start();
	lspRange.end.line = range.line->sourceFileLineIndex;
	lspRange.end.character = range.end();
	return lspRange;
}

Diagnostic TbxServer::convertDiagnostic(const ::Diagnostic &diag) const {
	Diagnostic lspDiag;
	lspDiag.range = convertRange(diag.range);
	lspDiag.message = diag.message;
	lspDiag.source = "3bx";

	switch (diag.level) {
	case ::Diagnostic::Level::Error:
		lspDiag.severity = DiagnosticSeverity::Error;
		break;
	case ::Diagnostic::Level::Warning:
		lspDiag.severity = DiagnosticSeverity::Warning;
		break;
	case ::Diagnostic::Level::Info:
		lspDiag.severity = DiagnosticSeverity::Information;
		break;
	}

	return lspDiag;
}

void TbxServer::publishDiagnostics(const std::string &uri, const std::vector<Diagnostic> &diagnostics) {
	PublishDiagnosticsParams params;
	params.uri = uri;
	params.diagnostics = diagnostics;
	sendNotification("textDocument/publishDiagnostics", params);
}

std::optional<Location> TbxServer::onDefinition(const TextDocumentPositionParams &params) {
	auto info = findElementAtPosition(params.textDocument.uri, params.position);

	// If it's a variable reference with a definition, go to the definition
	if (info.variableRef && info.variableRef->definition) {
		Location loc;
		loc.uri = info.variableRef->definition->range.line->sourceFile->uri;
		loc.range = convertRange(info.variableRef->definition->range);
		return loc;
	}

	// If it's a pattern reference, try to find the corresponding section
	if (info.patternRef && info.section) {
		// Find the section that defines this pattern
		// For now, return the section's first line
		if (!info.section->codeLines.empty()) {
			CodeLine *firstLine = info.section->codeLines[0];
			Location loc;
			loc.uri = firstLine->sourceFile->uri;
			loc.range.start.line = firstLine->sourceFileLineIndex;
			loc.range.start.character = 0;
			loc.range.end.line = firstLine->sourceFileLineIndex;
			loc.range.end.character = static_cast<int>(firstLine->rightTrimmedText.length());
			return loc;
		}
	}

	return std::nullopt;
}

TbxServer::PositionInfo TbxServer::findElementAtPosition(const std::string &uri, const Position &pos) {
	PositionInfo info;

	auto ctxIt = parseContexts.find(uri);
	if (ctxIt == parseContexts.end()) {
		return info;
	}

	ParseContext *context = ctxIt->second.get();

	// Find the code line at this position
	if (pos.line < 0 || pos.line >= static_cast<int>(context->codeLines.size())) {
		return info;
	}

	// Search through code lines to find one matching the position
	for (CodeLine *codeLine : context->codeLines) {
		if (codeLine->sourceFileLineIndex != pos.line) {
			continue;
		}

		// Check if position is within this line's source file URI
		if (codeLine->sourceFile->uri != uri) {
			continue;
		}

		info.section = codeLine->section;

		// Search for variable references at this position
		if (codeLine->section) {
			for (auto &[name, refs] : codeLine->section->variableReferences) {
				for (VariableReference *ref : refs) {
					if (ref->range.line == codeLine && ref->range.start() <= pos.character &&
						pos.character <= ref->range.end()) {
						info.variableRef = ref;
						return info;
					}
				}
			}

			// Search for pattern references at this position
			for (PatternReference *ref : codeLine->section->patternReferences) {
				if (ref->range.line == codeLine && ref->range.start() <= pos.character && pos.character <= ref->range.end()) {
					info.patternRef = ref;
					return info;
				}
			}
		}

		break;
	}

	return info;
}

SemanticTokens TbxServer::onSemanticTokensFull(const SemanticTokensParams &params) {
	SemanticTokens result;
	result.data = generateSemanticTokens(params.textDocument.uri);
	return result;
}

std::vector<int> TbxServer::generateSemanticTokens(const std::string &uri) {
	auto ctxIt = parseContexts.find(uri);
	if (ctxIt == parseContexts.end()) {
		return {};
	}

	ParseContext *context = ctxIt->second.get();
	auto docIt = documents.find(uri);
	if (docIt == documents.end()) {
		return {};
	}

	SemanticTokenBuilder builder(docIt->second->lineCount());

	// Helper to add a token from a Range
	auto addToken = [&builder, &uri](const ::Range &range, SemanticTokenType type, bool isDefinition) {
		if (range.line->sourceFile->uri != uri) {
			return;
		}
		int modifiers = isDefinition ? (1 << static_cast<int>(SemanticTokenModifier::Definition)) : 0;
		builder.add(range.line->sourceFileLineIndex, {range.start(), range.end(), type, modifiers});
	};

	// Walk through the parse context and collect tokens
	// Order matters: variables first, then patterns (deeper submatches first)

	std::function<void(Section *)> processSection = [&](Section *section) {
		if (!section)
			return;

		// 1. First add variable reference tokens (highest priority)
		for (auto &[name, refs] : section->variableReferences) {
			for (VariableReference *ref : refs) {
				bool isDefinition = (section->variableDefinitions.count(name) && section->variableDefinitions.at(name) == ref);
				addToken(ref->range, SemanticTokenType::Variable, isDefinition);
			}
		}

		// Recursively process children first (they have higher priority for patterns)
		for (Section *child : section->children) {
			processSection(child);
		}
	};

	// First pass: collect all variable tokens
	processSection(context->mainSection);

	// Second pass: add pattern definitions (sliced around variables)
	std::function<void(Section *)> processPatternDefs = [&](Section *section) {
		if (!section)
			return;

		// Process pattern definitions in this section
		for (PatternDefinition *def : section->patternDefinitions) {
			addToken(def->range, SemanticTokenType::PatternDefinition, true);
		}

		// Recursively process children
		for (Section *child : section->children) {
			processPatternDefs(child);
		}
	};

	processPatternDefs(context->mainSection);

	// Third pass: add pattern references (sliced around variables and definitions)
	std::function<void(Section *)> processPatternRefs = [&](Section *section) {
		if (!section)
			return;

		// Process pattern references
		for (PatternReference *ref : section->patternReferences) {
			SemanticTokenType tokenType;
			switch (ref->patternType) {
			case SectionType::Expression:
				tokenType = SemanticTokenType::Expression;
				break;
			case SectionType::Effect:
				tokenType = SemanticTokenType::Effect;
				break;
			case SectionType::Section:
				tokenType = SemanticTokenType::Section;
				break;
			default:
				tokenType = SemanticTokenType::PatternDefinition;
				break;
			}
			addToken(ref->range, tokenType, false);
		}

		// Recursively process children
		for (Section *child : section->children) {
			processPatternRefs(child);
		}
	};

	processPatternRefs(context->mainSection);

	// Also scan for comments (lowest priority, sliced around everything)
	for (CodeLine *line : context->codeLines) {
		if (line->sourceFile->uri != uri) {
			continue;
		}

		size_t commentPos = line->fullText.find('#');
		if (commentPos != std::string::npos) {
			size_t endPos = line->fullText.find_first_of("\r\n", commentPos);
			if (endPos == std::string::npos) {
				endPos = line->fullText.length();
			}
			builder.add(
				line->sourceFileLineIndex,
				{static_cast<int>(commentPos), static_cast<int>(endPos), SemanticTokenType::Comment, 0}
			);
		}
	}

	return builder.build();
}

} // namespace lsp
