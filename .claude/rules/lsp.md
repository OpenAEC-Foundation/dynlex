---
paths:
  - "src/lsp/**"
  - "vscode-extension/**"
---

# LSP Architecture (`src/lsp/`)

## VS Code extension debug workspaces
- Use workspace files to switch extension debug behavior for the same repo instead of toggling one shared `.vscode/settings.json`.
- `.vscode/lsp-managed.code-workspace`: managed mode (`dynlex.server.useExternal=false`) for normal extension development.
- `.vscode/lsp-debug.code-workspace`: external mode (`dynlex.server.useExternal=true`, `localhost:5008`) for C++ LSP debugging.
- `.vscode/launch.json` must keep separate extension host entries for managed and external workspaces; the `Extension + C++ Debugger` compound must target the external workspace entry.
- `Extension + C++ Debugger` must use compound-level `preLaunchTask: prepare external lsp debug` and no-prep child configs to avoid duplicate builds.
- `prepare external lsp debug` must run `build` then `compile extension` in sequence; if `build` fails, the compound must not launch either debugger session.

## Multi-file diagnostic tracking
- The LSP tracks an **import graph** (`importedBy`: imported URI → set of main URIs) and **cached diagnostics per main document** (`diagnosticsPerMain`: main URI → file URI → diagnostics).
- `onDidOpen`: if the file is already an import of an open main document, skip compilation (diagnostics already published). Otherwise compile as a new main document.
- `onDidChange`: recompile the file if it's a main document. Also recompile all main documents that import it (via `importedBy`).
- `onDidClose`: clean up state, re-publish merged diagnostics for affected files.
- Diagnostics are **grouped by source file URI** and published separately to each file. When multiple main documents produce diagnostics for the same file, they're merged with deduplication (same range + message = same diagnostic).
- `generateSemanticTokens` only suppresses tokens for errors **in the requested file**, not errors in imported files.

## Completion stability while editing broken lines
- Keep **latest compile state** and **completion state** separate.
- `parseContexts` + `importedBy` track the most recent compile attempt and drive diagnostics, hover, definition, document symbols, and semantic tokens.
- `completionParseContexts` + `completionImportedBy` track the last context for each main document that reached `ResolvedPatterns`.
- `textDocument/completion` must use the stable completion context together with the live current-line prefix from the open document.
- Do not replace the stable completion context when a recompile fails before `ResolvedPatterns`; otherwise tab completions collapse as soon as the user briefly leaves and re-enters a broken line.

## Document symbols (`textDocument/documentSymbol`)
- Returns hierarchical `DocumentSymbol[]` for VS Code outline view, breadcrumbs, and Ctrl+Shift+O navigation.
- Walks section tree recursively. Each section with `patternDefinitions` becomes a symbol.
- **Name**: concatenated from `patternElements` text directly (elements already include spaces as separate `Other`-type elements — do NOT add extra separators).
- **Detail**: `sectionTypeToString()`, prefixed with "macro " if `isMacro`.
- **Kind mapping**: Expression/Effect → `Function`, Class → `Class`, Pattern → `Module`, others → `Namespace`.
- **Range**: starts from `selectionRange` (pattern definition line), extends to last body code line. LSP requires `selectionRange ⊆ range`.
- Sections without pattern definitions (e.g. main section) pass children through to the parent symbol list.

## Semantic token types
- `Expression` (function-colored), `Effect` (keyword-colored), `Type` (type-colored), `Section`, `Variable`, `Comment`, `PatternDefinition`, `Number`, `String`, `Intrinsic`
- PatternCall tokens map `SectionType::Expression` → Expression, `SectionType::Class` → Type, others → Effect
- Token type IDs must match the legend order in `semanticTokens.h` and `package.json`

## Browser WASM LSP bridge
- The browser compiler reuses `DynLexServer` directly in-process (`src/web/webApi.cpp`) instead of duplicating hover/definition/token logic.
- Web LSP exports are JSON C ABI functions (`dynlex_web_get_lsp_hover_json`, `dynlex_web_get_lsp_definition_json`, `dynlex_web_get_lsp_semantic_tokens_json`) and must stay aligned with Monaco worker parsing in `web/src/worker/compilerWorker.js`.
- `DYNLEX_WEB` builds include LSP analysis sources, but `languageServer.cpp` must avoid TCP server mode in web builds.
- Browser-runner WASM imports must use `env.__memory_base = 0`; non-zero bases leave static string literal bytes unreadable in the worker runtime, causing empty `print` output.

## Diagnostic related information
- Compiler `Diagnostic` has a `relatedInfo` vector (`RelatedInfo{message, range}`) for linking to related source locations.
- The LSP converts these to `DiagnosticRelatedInformation` with proper absolute URIs, rendered as clickable links in VS Code.
- Example: "Duplicate pattern definition" links to the conflicting definition.
