#include "dynlexServer.h"
#include "codeLine.h"
#include "compiler.h"
#include "expression.h"
#include "lspFileSystem.h"
#include "patternMatch.h"
#include "patternTreeNode.h"
#include "section.h"
#include "semanticTokenBuilder.h"
#include "sourceFile.h"
#include <algorithm>
#include <regex>

namespace lsp {

DynLexServer::DynLexServer(int port) : LanguageServer(port) {}

DynLexServer::DynLexServer(std::unique_ptr<Transport> transport) : LanguageServer(std::move(transport)) {}

DynLexServer::~DynLexServer() = default;

InitializeResult DynLexServer::onInitialize(const InitializeParams & /*params*/) {
	InitializeResult result;
	result.capabilities.textDocumentSync = 2; // Incremental
	result.capabilities.definitionProvider = true;
	result.capabilities.semanticTokensProvider.full = true;
	result.capabilities.semanticTokensProvider.legend.tokenTypes = getSemanticTokenTypes();
	result.capabilities.semanticTokensProvider.legend.tokenModifiers = getSemanticTokenModifiers();
	return result;
}

void DynLexServer::onDidOpen(const DidOpenTextDocumentParams &params) {
	LanguageServer::onDidOpen(params);
	recompileDocument(params.textDocument.uri);
}

void DynLexServer::onDidChange(const DidChangeTextDocumentParams &params) {
	LanguageServer::onDidChange(params);
	recompileDocument(params.textDocument.uri);
}

void DynLexServer::onDidClose(const DidCloseTextDocumentParams &params) {
	parseContexts.erase(params.textDocument.uri);
	LanguageServer::onDidClose(params);
}

void DynLexServer::recompileDocument(const std::string &uri) {
	auto docIt = documents.find(uri);
	if (docIt == documents.end()) {
		return;
	}

	// Create new parse context with LSP file system
	auto context = std::make_unique<ParseContext>();
	LspFileSystem lspFs(documents);
	context->fileSystem = &lspFs;

	// Use the compiler to parse and analyze
	compile(uri, *context);

	// Convert diagnostics
	std::vector<Diagnostic> lspDiagnostics;
	for (const auto &diag : context->diagnostics) {
		lspDiagnostics.push_back(convertDiagnostic(diag));
	}

	// Store context and publish diagnostics
	parseContexts[uri] = std::move(context);
	publishDiagnostics(uri, lspDiagnostics);
}

Range DynLexServer::convertRange(const ::Range &range) const {
	Range lspRange;
	lspRange.start.line = range.line->sourceFileLineIndex;
	lspRange.start.character = range.start();
	lspRange.end.line = range.line->sourceFileLineIndex;
	lspRange.end.character = range.end();
	return lspRange;
}

Diagnostic DynLexServer::convertDiagnostic(const ::Diagnostic &diag) const {
	Diagnostic lspDiag;
	lspDiag.range = convertRange(diag.range);
	lspDiag.message = diag.message;
	lspDiag.source = "dynlex";

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

void DynLexServer::publishDiagnostics(const std::string &uri, const std::vector<Diagnostic> &diagnostics) {
	PublishDiagnosticsParams params;
	params.uri = uri;
	params.diagnostics = diagnostics;
	sendNotification("textDocument/publishDiagnostics", params);
}

std::optional<Location> DynLexServer::onDefinition(const TextDocumentPositionParams &params) {
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

DynLexServer::PositionInfo DynLexServer::findElementAtPosition(const std::string &uri, const Position &pos) {
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
				if (ref->range().line == codeLine && ref->range().start() <= pos.character &&
					pos.character <= ref->range().end()) {
					info.patternRef = ref;
					return info;
				}
			}
		}

		break;
	}

	return info;
}

SemanticTokens DynLexServer::onSemanticTokensFull(const SemanticTokensParams &params) {
	SemanticTokens result;
	result.data = generateSemanticTokens(params.textDocument.uri);
	return result;
}

std::vector<int> DynLexServer::generateSemanticTokens(const std::string &uri) {
	auto ctxIt = parseContexts.find(uri);
	if (ctxIt == parseContexts.end()) {
		return {};
	}

	ParseContext *context = ctxIt->second.get();
	bool hasErrors = std::any_of(context->diagnostics.begin(), context->diagnostics.end(), [](const ::Diagnostic &d) {
		return d.level == ::Diagnostic::Level::Error;
	});
	if (hasErrors) {
		return {};
	}
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
	// Order: variables → pattern matches → pattern definitions → comments (small to big, earlier slices later)

	std::function<void(Section *)> tokenizeVariables = [&](Section *section) {
		for (auto &[name, refs] : section->variableReferences) {
			for (VariableReference *ref : refs) {
				addToken(ref->range, SemanticTokenType::Variable, ref->isDefinition());
			}
		}
		for (Section *child : section->children) {
			tokenizeVariables(child);
		}
	};

	tokenizeVariables(context->mainSection);

	// Walk expression trees depth-first (children before parent, small tokens slice big ones)
	std::function<void(const Expression *, CodeLine *)> tokenizeExpression = [&](const Expression *expr, CodeLine *line) {
		// Tokenize arguments first (depth-first)
		for (const Expression *arg : expr->arguments) {
			tokenizeExpression(arg, line);
		}

		switch (expr->kind) {
		case Expression::Kind::Literal:
			if (std::holds_alternative<std::string>(expr->literalValue)) {
				addToken(expr->range, SemanticTokenType::String, false);
			} else if (std::holds_alternative<int64_t>(expr->literalValue) ||
					   std::holds_alternative<double>(expr->literalValue)) {
				addToken(expr->range, SemanticTokenType::Number, false);
			}
			break;
		case Expression::Kind::IntrinsicCall:
			addToken(expr->range, SemanticTokenType::Intrinsic, false);
			break;
		case Expression::Kind::PatternCall:
			if (expr->patternMatch && expr->patternMatch->matchedEndNode &&
				expr->patternMatch->matchedEndNode->matchingDefinition) {
				SectionType sectionType = expr->patternMatch->matchedEndNode->matchingDefinition->section->type;
				SemanticTokenType tokenType =
					sectionType == SectionType::Expression ? SemanticTokenType::Expression : SemanticTokenType::Effect;
				addToken(expr->range, tokenType, false);
			}
			break;
		default:
			break;
		}
	};

	for (CodeLine *line : context->codeLines) {
		if (line->sourceFile->uri != uri || !line->expression)
			continue;
		tokenizeExpression(line->expression, line);
	}

	std::function<void(Section *)> tokenizePatternDefinitions = [&](Section *section) {
		for (PatternDefinition *def : section->patternDefinitions) {
			addToken(def->range, SemanticTokenType::PatternDefinition, true);
		}
		for (Section *child : section->children) {
			tokenizePatternDefinitions(child);
		}
	};

	tokenizePatternDefinitions(context->mainSection);

	// Section openings cover entire lines
	for (CodeLine *line : context->codeLines) {
		if (line->sourceFile->uri != uri || !line->sectionOpening)
			continue;
		addToken(::Range(line, line->rightTrimmedText), SemanticTokenType::Section, false);
	}

	// Comments (lowest priority, sliced around everything)
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
