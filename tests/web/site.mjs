import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";

const webRoot = path.resolve(process.argv[2] || "web");
const homepagePath = path.join(webRoot, "index.html");
const requiredFiles = [homepagePath, path.join(webRoot, "home.css"), path.join(webRoot, "home.js")];

for (const filePath of requiredFiles) {
  if (!fs.existsSync(filePath)) {
    throw new Error(`Missing required website file: ${path.relative(webRoot, filePath)}`);
  }
}

const html = fs.readFileSync(homepagePath, "utf8");
const css = fs.readFileSync(path.join(webRoot, "home.css"), "utf8");
const script = fs.readFileSync(path.join(webRoot, "home.js"), "utf8");
const compiler = process.argv[3] ? path.resolve(process.argv[3]) : null;

const ids = Array.from(html.matchAll(/\bid="([^"]+)"/g), (match) => match[1]);
const duplicateIds = ids.filter((id, index) => ids.indexOf(id) !== index);
if (duplicateIds.length > 0) {
  throw new Error(`Homepage contains duplicate IDs: ${Array.from(new Set(duplicateIds)).join(", ")}`);
}

for (const requiredId of ["main", "language", "toolchain", "status", "start-title"]) {
  if (!ids.includes(requiredId)) {
    throw new Error(`Homepage is missing required section ID: ${requiredId}`);
  }
}

for (const requiredText of [
  "statically typed",
  "Pattern-defined syntax",
  "Native LLVM",
  "WebAssembly",
  "SPIR-V",
  "LSP + DAP",
  "Linux, macOS, and Windows packages",
  "Early. Working."
]) {
  if (!html.includes(requiredText)) {
    throw new Error(`Homepage is missing important project information: ${requiredText}`);
  }
}

for (const removedClaim of ["No language is faster", "Supreme Performance", "maintaining decades of legacy complexity"]) {
  if (html.includes(removedClaim)) {
    throw new Error(`Homepage still contains obsolete marketing copy: ${removedClaim}`);
  }
}

const tabs = Array.from(html.matchAll(/data-code-tab="([^"]+)"/g), (match) => match[1]);
const panels = Array.from(html.matchAll(/data-code-panel="([^"]+)"/g), (match) => match[1]);
if (tabs.length < 3 || tabs.join("\0") !== panels.join("\0")) {
  throw new Error(`Code tabs and panels differ: tabs=${tabs.join(",")} panels=${panels.join(",")}`);
}

const references = Array.from(html.matchAll(/\b(?:href|src)="([^"]+)"/g), (match) => match[1]);
for (const reference of references) {
  if (/^(?:https?:|mailto:|#|data:)/.test(reference)) {
    continue;
  }
  const cleanReference = reference.split(/[?#]/, 1)[0];
  if (!cleanReference) {
    continue;
  }
  const target = path.resolve(webRoot, cleanReference.replace(/^\//, ""));
  if (!fs.existsSync(target)) {
    throw new Error(`Homepage has a broken local reference: ${reference}`);
  }
}

const jsonLdMatch = html.match(/<script type="application\/ld\+json">([\s\S]*?)<\/script>/);
if (!jsonLdMatch) {
  throw new Error("Homepage is missing structured software metadata");
}
JSON.parse(jsonLdMatch[1]);

if (!css.includes("prefers-reduced-motion") || !css.includes(":focus-visible")) {
  throw new Error("Homepage styles must preserve reduced-motion and keyboard-focus accessibility");
}
if (!script.includes("ArrowLeft") || !script.includes("aria-selected") || !script.includes("TextEncoder")) {
  throw new Error("Homepage interactions are missing keyboard tabs or IDE example encoding");
}

if (compiler) {
  if (!fs.existsSync(compiler)) {
    throw new Error(`DynLex compiler does not exist: ${compiler}`);
  }
  const exampleMatches = Array.from(html.matchAll(/<div class="code-panel[^>]*data-code-panel="([^"]+)"[^>]*>[\s\S]*?<code>([\s\S]*?)<\/code>/g));
  if (exampleMatches.length !== panels.length) {
    throw new Error(`Expected ${panels.length} compilable homepage examples, found ${exampleMatches.length}`);
  }

  const temporaryDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "dynlex-homepage-"));
  try {
    for (const [, name, source] of exampleMatches) {
      const sourcePath = path.join(temporaryDirectory, `${name}.dl`);
      const outputPath = path.join(temporaryDirectory, `${name}.ll`);
      fs.writeFileSync(sourcePath, `${source.trim()}\n`);
      const result = spawnSync(compiler, [sourcePath, "--emit-llvm", "-o", outputPath], {
        cwd: path.dirname(webRoot),
        encoding: "utf8"
      });
      if (result.status !== 0 || !fs.existsSync(outputPath)) {
        throw new Error(`Homepage example "${name}" does not compile:\n${result.stdout}${result.stderr}`);
      }
    }
  } finally {
    fs.rmSync(temporaryDirectory, { recursive: true, force: true });
  }
}

console.log(`DynLex website checks passed (${references.length} homepage references, ${ids.length} unique IDs${compiler ? `, ${panels.length} compiled examples` : ""})`);
