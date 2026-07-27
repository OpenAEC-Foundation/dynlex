import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const testDirectory = path.dirname(fileURLToPath(import.meta.url));
const projectDirectory = path.resolve(testDirectory, "../..");
const webDirectory = path.join(projectDirectory, "web");
const wikiDirectory = path.join(webDirectory, "wiki");
const documentationPages = [
  ...fs.readdirSync(wikiDirectory)
    .filter((name) => name.endsWith(".html"))
    .map((name) => path.join(wikiDirectory, name)),
  ...fs.readdirSync(path.join(wikiDirectory, "sections"))
    .filter((name) => name.endsWith(".html"))
    .map((name) => path.join(wikiDirectory, "sections", name))
];
const navigationPath = path.join(webDirectory, "site-navigation.js");
const wikiActionsPath = path.join(wikiDirectory, "wiki-actions.js");
const wikiStylePath = path.join(wikiDirectory, "wiki.css");

for (const filePath of [navigationPath, wikiActionsPath, wikiStylePath, ...documentationPages]) {
  assert.ok(fs.existsSync(filePath), `Missing documentation asset: ${path.relative(projectDirectory, filePath)}`);
  assert.ok(
    fs.readFileSync(filePath, "utf8").split("\n").length < 1000,
    `${path.relative(projectDirectory, filePath)} must stay under 1000 lines`
  );
}

const navigationSource = fs.readFileSync(navigationPath, "utf8");
assert.match(navigationSource, /export function initializeSiteNavigation/);
assert.match(navigationSource, /classList\.toggle\("is-open", open\)/);
assert.match(navigationSource, /classList\.toggle\("menu-open", open\)/);
assert.match(navigationSource, /aria-current/);
assert.match(navigationSource, /wiki\/index\.html/);

const wikiActionsSource = fs.readFileSync(wikiActionsPath, "utf8");
assert.match(wikiActionsSource, /initializeSiteNavigation\(\)/);
assert.match(wikiActionsSource, /from "\.\.\/site-navigation\.js"/);

const wikiStyle = fs.readFileSync(wikiStylePath, "utf8");
assert.match(wikiStyle, /var\(--ink-1\)/);
assert.match(wikiStyle, /var\(--acid\)/);
assert.match(wikiStyle, /var\(--line\)/);
assert.doesNotMatch(wikiStyle, /--text-secondary|--code-bg|glass-card|bg-orbs|linear-gradient/);

for (const pagePath of documentationPages) {
  const html = fs.readFileSync(pagePath, "utf8");
  const relativePage = path.relative(wikiDirectory, pagePath);
  const inSectionDirectory = relativePage.startsWith(`sections${path.sep}`);
  const stylesheetPrefix = inSectionDirectory ? "../../" : "../";
  const wikiPrefix = inSectionDirectory ? "../" : "";

  assert.match(html, /<body class="docs-page">/);
  assert.match(html, /<a class="skip-link" href="#main-content">Skip to content<\/a>/);
  assert.match(html, /<header class="site-header" data-site-header><\/header>/);
  assert.match(html, /<main class="wiki-shell" id="main-content">/);
  assert.match(
    html,
    new RegExp(`<link rel="stylesheet" href="${stylesheetPrefix.replaceAll(".", "\\.")}responsive\\.css">`)
  );
  assert.match(
    html,
    new RegExp(`<script type="module" src="${wikiPrefix.replaceAll(".", "\\.")}wiki-actions\\.js"></script>`)
  );
  assert.doesNotMatch(html, /\b(?:nav-btn|glass-card|bg-orbs|orb-[123])\b/);
}

console.log("Documentation shares the homepage navigation and visual system.");
