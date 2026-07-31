import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const testDir = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.resolve(testDir, "../..");
const ideDir = path.join(projectDir, "src/web/ide");
const files = {
  html: path.join(ideDir, "index.html"),
  css: path.join(ideDir, "src/styles.css"),
  javascript: path.join(ideDir, "src/main.js"),
  lspClient: path.join(projectDir, "web/lsp-client.js"),
  lspIntegration: path.join(ideDir, "src/lspIntegration.js"),
  lspProtocol: path.join(ideDir, "src/lspProtocol.js"),
  compilerWorker: path.join(ideDir, "public/compiler/compiler-worker.js")
};

for (const filePath of Object.values(files)) {
  assert.ok(fs.existsSync(filePath), `Missing IDE file: ${path.relative(projectDir, filePath)}`);
  const lineCount = fs.readFileSync(filePath, "utf8").split("\n").length;
  assert.ok(lineCount < 1000, `${path.relative(projectDir, filePath)} must stay under 1000 lines`);
}

const html = fs.readFileSync(files.html, "utf8");
const javascript = fs.readFileSync(files.javascript, "utf8");
const lspIntegration = fs.readFileSync(files.lspIntegration, "utf8");
const lspClient = fs.readFileSync(files.lspClient, "utf8");
const languageJavascript = `${javascript}\n${lspIntegration}`;
const compilerWorker = fs.readFileSync(files.compilerWorker, "utf8");
assert.match(html, /<a[^>]+class="ide-brand"[^>]+href="\/"/);
assert.match(html, /<img[^>]+class="ide-logo"[^>]+src="\/icons\/dynlex-icon\.svg"/);
assert.doesNotMatch(html, /LLVM|WebAssembly|WASM|compiler runs/i);
assert.doesNotMatch(html, /\sstyle="/i, "IDE presentation belongs in styles.css");
assert.match(javascript, /monaco-editor\/esm\/vs\/editor\/editor\.api\.js/);
for (const contribution of [
  "codeAction/browser/codeActionContributions.js",
  "documentSymbols/browser/documentSymbols.js",
  "gotoSymbol/browser/goToCommands.js",
  "gotoSymbol/browser/link/goToDefinitionAtPosition.js",
  "hover/browser/hoverContribution.js",
  "semanticTokens/browser/documentSemanticTokens.js",
  "semanticTokens/browser/viewportSemanticTokens.js",
  "suggest/browser/suggestController.js"
]) {
  assert.match(javascript, new RegExp(contribution.replaceAll(".", "\\.")));
}
assert.doesNotMatch(javascript, /editor\.all\.js|wordHighlighter/);
assert.doesNotMatch(javascript, /from "monaco-editor"/, "IDE must not bundle Monaco's unused language catalog");
assert.doesNotMatch(
  languageJavascript,
  /setMonarchTokensProvider/,
  "DynLex token classification must come from the DynLex language server"
);
assert.match(languageJavascript, /new LspSession/);
assert.doesNotMatch(languageJavascript, /initializeLsp|shutdownLsp|new LspClient/);
assert.match(lspClient, /dynlex\/activeCursorChanged/);
for (const provider of [
  "registerCompletionItemProvider",
  "registerDefinitionProvider",
  "registerHoverProvider",
  "registerDocumentSemanticTokensProvider",
  "registerDocumentSymbolProvider",
  "registerCodeActionProvider"
]) {
  assert.match(languageJavascript, new RegExp(provider), `IDE must register ${provider}`);
}
for (const method of [
  "textDocument/completion",
  "textDocument/definition",
  "textDocument/hover",
  "textDocument/semanticTokens/full",
  "textDocument/documentSymbol",
  "textDocument/codeAction",
  "dynlex/instantiationsInDocument",
  "dynlex/selectInstantiation",
  "dynlex/readDocument"
]) {
  assert.match(languageJavascript, new RegExp(method.replace("/", "\\/")), `IDE must use ${method}`);
}
assert.match(compilerWorker, /"lsp\.exchange"/);
assert.match(compilerWorker, /dynlex_web_lsp_exchange_json/);
assert.doesNotMatch(compilerWorker, /dynlex_web_get_lsp_(?:hover|definition|semantic_tokens)_json/);
assert.doesNotMatch(
  html,
  /Start with a thought|Put the idea into words|Follow any useful feedback|See what your idea does|WHAT TO LOOK AT|Check code/i,
  "The IDE must be a workspace, not a three-step tutorial"
);
assert.doesNotMatch(html, /class="start-guide"/, "The IDE must not prescribe a write-check-run sequence");
assert.doesNotMatch(html, /id="compile-button"/, "Live analysis makes a separate check button redundant");
assert.match(html, /<aside class="project-panel" aria-label="Project files">/);
assert.match(javascript, /new Worker\("\/compiler\/compiler-worker\.js"/);
assert.match(javascript, /URLSearchParams/);
assert.match(javascript, /mode.*shader/);
assert.match(javascript, /shaders\/manifest\.json/);
assert.doesNotMatch(javascript, /renderer64/);
assert.match(javascript, /from "\.\.\/\.\.\/\.\.\/\.\.\/web\/shader-renderer\.js"/);
assert.match(compilerWorker, /compile\.shader/);
assert.match(compilerWorker, /dynlex_web_compile_and_emit_shader_glsl/);
assert.match(compilerWorker, /compileShaderStage/);
assert.match(compilerWorker, /"vertex"/);
assert.match(compilerWorker, /"fragment"/);
assert.match(compilerWorker, /dynlex_web_get_output_shader_glsl/);
assert.match(javascript, /scrollbarSlider\.background/);
assert.match(javascript, /scrollbarSlider\.hoverBackground/);
assert.match(javascript, /scrollbarSlider\.activeBackground/);

for (const requiredId of [
  "editor",
  "run-button",
  "theme-button",
  "status-pill",
  "diagnostics-empty",
  "diagnostics-list",
  "compiler-log",
  "runtime-output",
  "shader-preview"
]) {
  assert.match(html, new RegExp(`id="${requiredId}"`), `Missing IDE element #${requiredId}`);
}

const ids = [...html.matchAll(/\sid="([^"]+)"/g)].map((match) => match[1]);
assert.equal(new Set(ids).size, ids.length, "IDE element IDs must be unique");

for (const match of html.matchAll(/\saria-controls="([^"]+)"/g)) {
  assert.ok(ids.includes(match[1]), `IDE aria-controls references missing ID: ${match[1]}`);
}

assert.equal((html.match(/role="tab"/g) ?? []).length, 3, "IDE must expose three workspace tabs");
assert.equal((html.match(/role="tabpanel"/g) ?? []).length, 3, "IDE must expose three workspace panels");

console.log("IDE shell structure and messaging are valid.");
