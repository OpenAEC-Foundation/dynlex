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
#include <filesystem>
#include <regex>

namespace lsp {

// Ensure a URI is an absolute file:// URI.
// Imported files may have relative paths (e.g. "lib/random.dl") that need
// to be resolved to absolute file:// URIs for the LSP client.
static std::string toAbsoluteUri(const std::string &uri) {
	if (uri.starts_with("file://")) {
		return uri;
	}
	return "file://" + std::filesystem::absolute(uri).string();
}

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
	context->fileSystem = std::make_unique<LspFileSystem>(documents);

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

// Find the deepest expression containing the cursor position.
// Depth-first: children (subexpressions) take priority over parents,
// matching the semantic tokenizer's slicing behavior.
static Expression *findDeepestExpression(Expression *expr, int character) {
	for (Expression *arg : expr->arguments) {
		if (arg->range.start() <= character && character < arg->range.end()) {
			Expression *deeper = findDeepestExpression(arg, character);
			if (deeper)
				return deeper;
		}
	}
	if (expr->range.start() <= character && character < expr->range.end()) {
		return expr;
	}
	return nullptr;
}

// Get the definition location for an expression, following the same
// semantic categories as the tokenizer (Variable, PatternCall, etc.)
static std::optional<::Range> getDefinitionTarget(Expression *expr) {
	switch (expr->kind) {
	case Expression::Kind::Variable:
		if (expr->variable && expr->variable->definition) {
			return expr->variable->definition->range;
		}
		break;
	case Expression::Kind::PatternCall:
		if (expr->patternMatch && expr->patternMatch->matchedEndNode &&
			expr->patternMatch->matchedEndNode->matchingDefinition) {
			return expr->patternMatch->matchedEndNode->matchingDefinition->range;
		}
		break;
	default:
		break;
	}
	return std::nullopt;
}

std::optional<Location> DynLexServer::onDefinition(const TextDocumentPositionParams &params) {
	auto ctxIt = parseContexts.find(params.textDocument.uri);
	if (ctxIt == parseContexts.end()) {
		return std::nullopt;
	}

	ParseContext *context = ctxIt->second.get();

	// Find the code line at the cursor position
	for (CodeLine *codeLine : context->codeLines) {
		if (codeLine->sourceFileLineIndex != params.position.line) {
			continue;
		}
		if (codeLine->sourceFile->uri != params.textDocument.uri) {
			continue;
		}

		// Walk the expression tree to find the deepest expression at cursor
		if (codeLine->expression) {
			Expression *expr = findDeepestExpression(codeLine->expression, params.position.character);
			if (expr) {
				auto target = getDefinitionTarget(expr);
				if (target) {
					Location loc;
					loc.uri = toAbsoluteUri(target->line->sourceFile->uri);
					loc.range = convertRange(*target);
					return loc;
				}
			}
		}
		break;
	}

	return std::nullopt;
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
