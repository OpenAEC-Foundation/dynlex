import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const testDir = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.resolve(testDir, "../..");
const webDir = path.join(projectDir, "web");
const files = {
  html: path.join(webDir, "index.html"),
  css: path.join(webDir, "style.css"),
  shaderBannerCss: path.join(webDir, "shader-banner.css"),
  sectionsCss: path.join(webDir, "sections.css"),
  responsiveCss: path.join(webDir, "responsive.css"),
  navigation: path.join(webDir, "site-navigation.js"),
  javascript: path.join(webDir, "homepage.js"),
  lspClient: path.join(webDir, "lsp-client.js"),
  highlightCache: path.join(webDir, "snippet-highlights.js"),
  highlightKey: path.join(webDir, "snippet-highlight-key.js"),
  semanticHighlighter: path.join(webDir, "semantic-highlighting.js"),
  semanticTokenLegend: path.join(webDir, "semantic-token-legend.js")
};

for (const filePath of Object.values(files)) {
  assert.ok(fs.existsSync(filePath), `Missing homepage file: ${path.relative(projectDir, filePath)}`);
  const lineCount = fs.readFileSync(filePath, "utf8").split("\n").length;
  assert.ok(lineCount < 1000, `${path.relative(projectDir, filePath)} must stay under 1000 lines`);
}

const html = fs.readFileSync(files.html, "utf8");
const navigation = fs.readFileSync(files.navigation, "utf8");
assert.match(html, /<a[^>]+class="skip-link"[^>]+href="#main-content"/);
assert.match(html, /<header class="site-header" data-site-header><\/header>/);
assert.match(html, /<main[^>]+id="main-content"/);
assert.equal((html.match(/<h1\b/g) ?? []).length, 1, "Homepage must have exactly one h1");
assert.match(html, /<script type="module" src="homepage\.js"><\/script>/);
assert.doesNotMatch(html, /\sstyle="/i, "Homepage presentation belongs in style.css");
assert.doesNotMatch(html, /introCanvas|cpp_site\.png|python_site\.png/);
assert.doesNotMatch(
  html,
  /LLVM|WebAssembly|WASM|SPIR-V|machine code|compiler pipeline|static typ(?:e|ing)/i,
  "Implementation details belong in the documentation, not homepage copy"
);
assert.doesNotMatch(html, /class="brand-mark"/, "The transparent logo must not be placed on a forced tile");
assert.doesNotMatch(
  html,
  /Write your first line|TRY IT NOW|START NOW|AT A GLANCE|THE DIFFERENCE|YOUR LANGUAGE STARTS HERE/i,
  "The homepage must feel like a creative workspace, not a marketing funnel"
);
assert.doesNotMatch(html, /class="[^"]*(?:capability-grid|final-cta|idea-path)[^"]*"/);
assert.match(html, /<section[^>]+class="[^"]*hero-workbench[^"]*"/);
assert.match(html, /data-river-challenge/);
assert.match(html, /data-river-preview/);
assert.match(html, /data-river-challenge-load/);
assert.doesNotMatch(html, /challenge-door(?:-|\b)/, "The retired river trailer must be removed");
assert.doesNotMatch(html, /#sketches/, "Retired sketch links must be removed");
assert.equal((html.match(/data-runnable-sketch/g) ?? []).length, 3, "Homepage needs its focused runnable sketches");
assert.equal((html.match(/data-snippet-source/g) ?? []).length, 5, "Runnable sketches need editable source fields");
assert.equal((html.match(/data-snippet-run/g) ?? []).length, 3, "Runnable sketches need run controls");
assert.equal((html.match(/data-snippet-output/g) ?? []).length, 3, "Runnable sketches need inline output");
assert.match(html, /<textarea[^>]+data-snippet-source/);
assert.doesNotMatch(
  html,
  /class="studio-window[^"]*"[^>]*aria-hidden="true"/,
  "The studio sketch must be interactive and exposed to assistive technology"
);
assert.match(
  navigation,
  /href="\/download\.html"/,
  "Primary navigation must expose the DynLex download",
);
assert.match(html, /<section[^>]+id="install"/);
assert.match(html, /href="download\.html"/);
assert.match(
  html,
  /https:\/\/marketplace\.visualstudio\.com\/items\?itemName=impertio\.dynlex-language/,
);
assert.match(html, /https:\/\/open-vsx\.org\/extension\/open-aec\/dynlex-language/);
assert.match(
  html,
  /<a[^>]+href="https:\/\/discord\.gg\/aBmgCAYKke"[^>]+target="_blank"[^>]+rel="noopener noreferrer"[^>]*>Discord ↗<\/a>/,
  "Footer navigation must expose the DynLex Discord community",
);

const homepageJavascript = fs.readFileSync(files.javascript, "utf8");
const lspClientJavascript = fs.readFileSync(files.lspClient, "utf8");
const homepageCss = [
  fs.readFileSync(files.css, "utf8"),
  fs.readFileSync(files.shaderBannerCss, "utf8"),
  fs.readFileSync(files.sectionsCss, "utf8"),
  fs.readFileSync(files.responsiveCss, "utf8")
].join("\n");
assert.match(homepageJavascript, /fetch\("\/compiler\/manifest\.json", \{ cache: "no-store" \}\)/);
assert.match(homepageJavascript, /workerUrl\.searchParams\.set\("revision", revision\)/);
assert.match(homepageJavascript, /new Worker\(workerUrl/);
assert.match(homepageJavascript, /from "\.\/snippet-highlights\.js"/);
assert.match(homepageJavascript, /from "\.\/snippet-highlight-key\.js"/);
assert.match(homepageJavascript, /from "\.\/semantic-highlighting\.js"/);
assert.match(homepageJavascript, /textDocument\/semanticTokens\/full/);
assert.match(homepageJavascript, /new LspSession/);
assert.doesNotMatch(homepageJavascript, /initializeLsp|shutdownLsp|new LspClient/);
assert.match(
  homepageJavascript,
  /snippetLsp\.request\(\s*"dynlex\/callExpressions",\s*snippetLspDocument\.identifier\s*\)/
);
assert.match(lspClientJavascript, /dynlex\/activeCursorChanged/);
assert.match(homepageJavascript, /initializeSiteNavigation\(\)/);
assert.doesNotMatch(homepageJavascript, /function setMenu\(/);
assert.doesNotMatch(homepageJavascript, /function decodeSemanticTokenRanges/);
assert.doesNotMatch(homepageJavascript, /function semanticLegendsMatch/);
assert.doesNotMatch(
  homepageJavascript,
  /classList\.contains\("(?:hero|language|studio)-snippet-editor"\)/,
  "Snippet initialization must not branch for individual homepage editors"
);

const highlightCache = fs.readFileSync(files.highlightCache, "utf8");
assert.match(highlightCache, /semanticHighlightCache/);
assert.match(highlightCache, /new Map\(/);
assert.match(highlightCache, /from "\.\/semantic-token-legend\.js"/);
assert.doesNotMatch(highlightCache, /import lib\//, "Generated cache must not duplicate homepage source text");

const highlightKey = fs.readFileSync(files.highlightKey, "utf8");
assert.match(highlightKey, /SHA-256/);
const semanticHighlighter = fs.readFileSync(files.semanticHighlighter, "utf8");
assert.match(semanticHighlighter, /export function renderSemanticTokens/);
assert.match(semanticHighlighter, /export function semanticLegendsMatch/);
assert.match(homepageCss, /::-webkit-scrollbar-thumb/);
assert.match(homepageCss, /scrollbar-color:/);
assert.match(homepageCss, /\*::-webkit-scrollbar-track/);

for (const retiredAsset of ["cpp_site.png", "python_site.png"]) {
  assert.ok(!fs.existsSync(path.join(webDir, retiredAsset)), `Retired homepage asset still exists: ${retiredAsset}`);
}

for (const sectionId of ["playground", "challenges", "language", "studio", "install"]) {
  assert.match(html, new RegExp(`id="${sectionId}"`), `Missing homepage section #${sectionId}`);
}

const ids = [...html.matchAll(/\sid="([^"]+)"/g)].map((match) => match[1]);
assert.equal(new Set(ids).size, ids.length, "Homepage element IDs must be unique");

for (const match of html.matchAll(/\saria-controls="([^"]+)"/g)) {
  assert.ok(ids.includes(match[1]), `aria-controls references missing ID: ${match[1]}`);
}

const localReferencePattern = /(?:href|src)="([^"#][^"]*)"/g;
for (const match of html.matchAll(localReferencePattern)) {
  const reference = match[1];
  if (/^(?:https?:|mailto:|data:)/.test(reference)) {
    continue;
  }
  const withoutQueryOrHash = reference.split(/[?#]/, 1)[0];
  const target = path.resolve(webDir, withoutQueryOrHash);
  assert.ok(fs.existsSync(target), `Broken local homepage reference: ${reference}`);
}

for (const anchor of html.matchAll(/<a\b([^>]*)>/g)) {
  const attributes = anchor[1];
  if (/target="_blank"/.test(attributes)) {
    assert.match(attributes, /rel="[^"]*noopener[^"]*"/, "New-tab links must use rel=noopener");
  }
}

console.log("Homepage structure and local references are valid.");
