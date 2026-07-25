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
  javascript: path.join(webDir, "homepage.js"),
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
assert.match(html, /<a[^>]+class="skip-link"[^>]+href="#main-content"/);
assert.match(html, /<nav[^>]+aria-label="Primary navigation"/);
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
assert.match(html, /class="sketch-grid"/);
assert.ok((html.match(/data-runnable-sketch/g) ?? []).length >= 5, "Homepage needs at least five runnable sketches");
assert.ok((html.match(/data-snippet-source/g) ?? []).length >= 5, "Runnable sketches need editable source fields");
assert.ok((html.match(/data-snippet-run/g) ?? []).length >= 5, "Runnable sketches need run controls");
assert.ok((html.match(/data-snippet-output/g) ?? []).length >= 5, "Runnable sketches need inline output");
assert.match(html, /<textarea[^>]+data-snippet-source/);

const homepageJavascript = fs.readFileSync(files.javascript, "utf8");
const homepageCss = [
  fs.readFileSync(files.css, "utf8"),
  fs.readFileSync(files.shaderBannerCss, "utf8"),
  fs.readFileSync(files.sectionsCss, "utf8"),
  fs.readFileSync(files.responsiveCss, "utf8")
].join("\n");
assert.match(homepageJavascript, /new Worker\("\/compiler\/compiler-worker\.js"/);
assert.match(homepageJavascript, /from "\.\/snippet-highlights\.js"/);
assert.match(homepageJavascript, /from "\.\/snippet-highlight-key\.js"/);
assert.match(homepageJavascript, /from "\.\/semantic-highlighting\.js"/);
assert.match(homepageJavascript, /lsp\.semanticTokens/);
assert.doesNotMatch(homepageJavascript, /function decodeSemanticTokenRanges/);
assert.doesNotMatch(homepageJavascript, /function semanticLegendsMatch/);

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

for (const sectionId of ["playground", "sketches", "language", "studio"]) {
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
