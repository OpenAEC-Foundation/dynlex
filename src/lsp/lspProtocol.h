#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using Json = nlohmann::json;

namespace lsp {

// Basic LSP types

struct Position {
	int line = 0;
	int character = 0;
};

inline void to_json(Json &j, const Position &p) { j = Json{{"line", p.line}, {"character", p.character}}; }

inline void from_json(const Json &j, Position &p) {
	j.at("line").get_to(p.line);
	j.at("character").get_to(p.character);
}

struct Range {
	Position start;
	Position end;
};

inline void to_json(Json &j, const Range &r) { j = Json{{"start", r.start}, {"end", r.end}}; }

inline void from_json(const Json &j, Range &r) {
	j.at("start").get_to(r.start);
	j.at("end").get_to(r.end);
}

struct Location {
	std::string uri;
	Range range;
};

inline void to_json(Json &j, const Location &l) { j = Json{{"uri", l.uri}, {"range", l.range}}; }

inline void from_json(const Json &j, Location &l) {
	j.at("uri").get_to(l.uri);
	j.at("range").get_to(l.range);
}

// Diagnostic severity levels (1-indexed per LSP spec)
enum class DiagnosticSeverity { Error = 1, Warning = 2, Information = 3, Hint = 4 };

struct Diagnostic {
	Range range;
	std::optional<DiagnosticSeverity> severity;
	std::string message;
	std::optional<std::string> source;
};

inline void to_json(Json &j, const Diagnostic &d) {
	j = Json{{"range", d.range}, {"message", d.message}};
	if (d.severity) {
		j["severity"] = static_cast<int>(*d.severity);
	}
	if (d.source) {
		j["source"] = *d.source;
	}
}

// Text document identifier
struct TextDocumentIdentifier {
	std::string uri;
};

inline void from_json(const Json &j, TextDocumentIdentifier &t) { j.at("uri").get_to(t.uri); }

struct VersionedTextDocumentIdentifier {
	std::string uri;
	int version = 0;
};

inline void from_json(const Json &j, VersionedTextDocumentIdentifier &t) {
	j.at("uri").get_to(t.uri);
	j.at("version").get_to(t.version);
}

// Text document item (for didOpen)
struct TextDocumentItem {
	std::string uri;
	std::string languageId;
	int version = 0;
	std::string text;
};

inline void from_json(const Json &j, TextDocumentItem &t) {
	j.at("uri").get_to(t.uri);
	j.at("languageId").get_to(t.languageId);
	j.at("version").get_to(t.version);
	j.at("text").get_to(t.text);
}

// Text document position params (for definition, hover, etc.)
struct TextDocumentPositionParams {
	TextDocumentIdentifier textDocument;
	Position position;
};

inline void from_json(const Json &j, TextDocumentPositionParams &p) {
	j.at("textDocument").get_to(p.textDocument);
	j.at("position").get_to(p.position);
}

// Content change event for incremental sync
struct TextDocumentContentChangeEvent {
	std::optional<Range> range;
	std::string text;
};

inline void from_json(const Json &j, TextDocumentContentChangeEvent &c) {
	if (j.contains("range")) {
		c.range = j.at("range").get<Range>();
	}
	j.at("text").get_to(c.text);
}

// didChange params
struct DidChangeTextDocumentParams {
	VersionedTextDocumentIdentifier textDocument;
	std::vector<TextDocumentContentChangeEvent> contentChanges;
};

inline void from_json(const Json &j, DidChangeTextDocumentParams &p) {
	j.at("textDocument").get_to(p.textDocument);
	j.at("contentChanges").get_to(p.contentChanges);
}

// didOpen params
struct DidOpenTextDocumentParams {
	TextDocumentItem textDocument;
};

inline void from_json(const Json &j, DidOpenTextDocumentParams &p) { j.at("textDocument").get_to(p.textDocument); }

// didClose params
struct DidCloseTextDocumentParams {
	TextDocumentIdentifier textDocument;
};

inline void from_json(const Json &j, DidCloseTextDocumentParams &p) { j.at("textDocument").get_to(p.textDocument); }

// didSave params
struct DidSaveTextDocumentParams {
	TextDocumentIdentifier textDocument;
	std::optional<std::string> text;
};

inline void from_json(const Json &j, DidSaveTextDocumentParams &p) {
	j.at("textDocument").get_to(p.textDocument);
	if (j.contains("text")) {
		p.text = j.at("text").get<std::string>();
	}
}

// Semantic tokens types
struct SemanticTokensLegend {
	std::vector<std::string> tokenTypes;
	std::vector<std::string> tokenModifiers;
};

inline void to_json(Json &j, const SemanticTokensLegend &l) {
	j = Json{{"tokenTypes", l.tokenTypes}, {"tokenModifiers", l.tokenModifiers}};
}

struct SemanticTokens {
	std::vector<int> data;
};

inline void to_json(Json &j, const SemanticTokens &t) { j = Json{{"data", t.data}}; }

struct SemanticTokensParams {
	TextDocumentIdentifier textDocument;
};

inline void from_json(const Json &j, SemanticTokensParams &p) { j.at("textDocument").get_to(p.textDocument); }

// Initialize params (simplified)
struct InitializeParams {
	std::optional<int> processId;
	std::optional<std::string> rootUri;
};

inline void from_json(const Json &j, InitializeParams &p) {
	if (j.contains("processId") && !j.at("processId").is_null()) {
		p.processId = j.at("processId").get<int>();
	}
	if (j.contains("rootUri") && !j.at("rootUri").is_null()) {
		p.rootUri = j.at("rootUri").get<std::string>();
	}
}

// Server capabilities
struct ServerCapabilities {
	// Text document sync kind: 0=None, 1=Full, 2=Incremental
	int textDocumentSync = 2;
	bool definitionProvider = true;
	struct {
		bool full = true;
		SemanticTokensLegend legend;
	} semanticTokensProvider;
};

inline void to_json(Json &j, const ServerCapabilities &c) {
	j = Json{
		{"textDocumentSync", c.textDocumentSync},
		{"definitionProvider", c.definitionProvider},
		{"semanticTokensProvider", Json{{"full", c.semanticTokensProvider.full}, {"legend", c.semanticTokensProvider.legend}}}
	};
}

struct InitializeResult {
	ServerCapabilities capabilities;
};

inline void to_json(Json &j, const InitializeResult &r) { j = Json{{"capabilities", r.capabilities}}; }

// PublishDiagnostics params
struct PublishDiagnosticsParams {
	std::string uri;
	std::vector<Diagnostic> diagnostics;
};

inline void to_json(Json &j, const PublishDiagnosticsParams &p) { j = Json{{"uri", p.uri}, {"diagnostics", p.diagnostics}}; }

} // namespace lsp
