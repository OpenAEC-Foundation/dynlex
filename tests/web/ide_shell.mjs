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
  packageJson: path.join(ideDir, "package.json"),
  viteConfig: path.join(ideDir, "vite.config.js"),
  lspClient: path.join(projectDir, "web/lsp-client.js"),
  lspIntegration: path.join(ideDir, "src/lspIntegration.js"),
  lspProtocol: path.join(ideDir, "src/lspProtocol.js"),
  browserDriver: path.join(projectDir, "tests/web/browser_test_driver.mjs"),
  compilerWorker: path.join(ideDir, "public/compiler/compiler-worker.js"),
  runtimeImports: path.join(ideDir, "public/compiler/runtimeImports.js")
};

for (const filePath of Object.values(files)) {
  assert.ok(fs.existsSync(filePath), `Missing IDE file: ${path.relative(projectDir, filePath)}`);
  const lineCount = fs.readFileSync(filePath, "utf8").split("\n").length;
  assert.ok(lineCount < 1000, `${path.relative(projectDir, filePath)} must stay under 1000 lines`);
}

const html = fs.readFileSync(files.html, "utf8");
const javascript = fs.readFileSync(files.javascript, "utf8");
const packageJson = JSON.parse(fs.readFileSync(files.packageJson, "utf8"));
const lspIntegration = fs.readFileSync(files.lspIntegration, "utf8");
const lspClient = fs.readFileSync(files.lspClient, "utf8");
const languageJavascript = `${javascript}\n${lspIntegration}`;
const compilerWorker = fs.readFileSync(files.compilerWorker, "utf8");
const runtimeImports = fs.readFileSync(files.runtimeImports, "utf8");
const browserDriver = fs.readFileSync(files.browserDriver, "utf8");
const viteConfig = fs.readFileSync(files.viteConfig, "utf8");
assert.equal(packageJson.dependencies["monaco-editor"], "^0.56.0");
assert.equal(packageJson.devDependencies.vite, "^8.2.2");
assert.equal(packageJson.overrides["monaco-editor"].dompurify, "3.4.14");
assert.match(html, /<a[^>]+class="ide-brand"[^>]+href="\/"/);
assert.match(html, /<img[^>]+class="ide-logo"[^>]+src="\/icons\/dynlex-icon\.svg"/);
assert.doesNotMatch(html, /LLVM|WebAssembly|WASM|compiler runs/i);
assert.doesNotMatch(html, /\sstyle="/i, "IDE presentation belongs in styles.css");
assert.match(javascript, /from "monaco-editor\/editor"/);
assert.match(javascript, /commitActiveLine/);
assert.match(javascript, /diagnosticsByUri/);
for (const contribution of [
  "clipboard",
  "codeAction",
  "codicon",
  "contextmenu",
  "documentSymbols",
  "find",
  "gotoSymbol",
  "hover",
  "readOnlyMessage",
  "semanticTokens"
]) {
  assert.match(javascript, new RegExp(`monaco-editor/features/${contribution}/register`));
}
assert.match(javascript, /monaco-editor\/editor\/editor\.worker\.js/);
assert.match(
  javascript,
  /monaco-editor\/editor\/contrib\/semanticTokens\/browser\/documentSemanticTokens/
);
assert.match(
  javascript,
  /monaco-editor\/editor\/contrib\/suggest\/browser\/suggestController/
);
assert.doesNotMatch(javascript, /monaco-editor\/features\/suggest\/register/);
assert.doesNotMatch(
  javascript,
  /monaco-editor\/esm\/vs\//,
  "IDE imports must use Monaco's supported tree-shakeable entry points"
);
assert.doesNotMatch(javascript, /editor\.all\.js|wordHighlighter/);
assert.doesNotMatch(javascript, /from "monaco-editor"/, "IDE must not bundle Monaco's unused language catalog");
assert.match(browserDriver, /\.native-edit-context/);
assert.doesNotMatch(browserDriver, /textarea\.(?:inputarea|ime-text-area)/);
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
assert.match(compilerWorker, /compilerRevision/);
assert.match(compilerWorker, /compilerAssetUrl/);
assert.match(
  compilerWorker,
  /const runtimeImportsPromise = import\(\/\* @vite-ignore \*\/ compilerAssetUrl\("runtimeImports\.js"\)\)/
);
assert.doesNotMatch(compilerWorker, /await import\(\/\* @vite-ignore \*\/ compilerAssetUrl\("runtimeImports\.js"\)\)/);
assert.match(compilerWorker, /versionedAssetUrl\("\/wgsl-translator\.js"\)/);
assert.match(compilerWorker, /createWgslTranslator\(compilerAssetUrl\("dynlex_wgsl_translator\.wasm"\)\)/);
assert.match(runtimeImports, /runtimeDependencyUrl\("\.\/runtimeFilesystem\.js"\)/);
assert.match(runtimeImports, /runtimeDependencyUrl\("\.\/runtimePathHost\.js"\)/);
assert.match(runtimeImports, /runtimeDependencyUrl\("\.\/runtimeLayout\.js"\)/);
assert.match(javascript, /__DYNLEX_COMPILER_REVISION__/);
assert.match(viteConfig, /fileName: "compiler\/manifest\.json"/);
assert.match(viteConfig, /readdirSync\(compilerDirectory/);
assert.doesNotMatch(compilerWorker, /dynlex_web_get_lsp_(?:hover|definition|semantic_tokens)_json/);
assert.doesNotMatch(
  html,
  /Start with a thought|Put the idea into words|Follow any useful feedback|See what your idea does|WHAT TO LOOK AT|Check code/i,
  "The IDE must be a workspace, not a three-step tutorial"
);
assert.doesNotMatch(html, /class="start-guide"/, "The IDE must not prescribe a write-check-run sequence");
assert.doesNotMatch(html, /id="compile-button"/, "Live analysis makes a separate check button redundant");
assert.match(html, /<aside id="project-panel" class="project-panel" aria-label="Project files">/);
assert.match(javascript, /new Worker\(compilerWorkerUrl/);
assert.match(javascript, /URLSearchParams/);
assert.match(javascript, /mode.*shader/);
assert.match(javascript, /shaders\/manifest\.json/);
assert.doesNotMatch(javascript, /renderer64/);
assert.match(javascript, /from "\.\.\/\.\.\/\.\.\/\.\.\/web\/shader-renderer\.js"/);
assert.match(compilerWorker, /compile\.shader/);
assert.match(compilerWorker, /dynlex_web_compile_and_emit_shader_spirv/);
assert.match(compilerWorker, /createWgslTranslator/);
assert.match(compilerWorker, /compileShaderStage/);
assert.match(compilerWorker, /"vertex"/);
assert.match(compilerWorker, /"fragment"/);
assert.match(compilerWorker, /dynlex_web_get_output_shader_spirv_base64/);
assert.doesNotMatch(compilerWorker, /WebGL|webgl|glsl/i);
assert.match(javascript, /scrollbarSlider\.background/);
assert.match(javascript, /scrollbarSlider\.hoverBackground/);
assert.match(javascript, /scrollbarSlider\.activeBackground/);

for (const requiredId of [
  "editor",
  "run-button",
  "theme-button",
  "project-panel-button",
  "tool-panel-button",
  "workspace-panel-backdrop",
  "project-panel",
  "tool-panel",
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
