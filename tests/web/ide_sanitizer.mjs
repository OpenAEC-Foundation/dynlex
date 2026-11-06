import assert from "node:assert/strict";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath, pathToFileURL } from "node:url";

const project = fileURLToPath(new URL("../../", import.meta.url));
const ide = path.join(project, "src/web/ide");
const require = createRequire(path.join(ide, "package.json"));
const { build } = await import(pathToFileURL(require.resolve("vite")));
const sanitizerDirectory = path.dirname(require.resolve("dompurify")).replaceAll("\\", "/") + "/";
const outputs = await build({
  root: ide,
  configFile: path.join(ide, "vite.config.js"),
  logLevel: "silent",
  build: { write: false }
});
const bundles = (Array.isArray(outputs) ? outputs : [outputs]).flatMap(output => output.output);
const modules = new Set(bundles.filter(output => output.type === "chunk").flatMap(chunk => Object.keys(chunk.modules)));
assert.equal(
  [...modules].filter(id => id.replaceAll("\\", "/").startsWith(sanitizerDirectory)).length,
  1,
  "The production editor must use one installed DOMPurify dependency"
);
assert.equal(
  [...modules].filter(id => /[/\\]monaco-editor[/\\].*[/\\]dompurify[/\\]/.test(id)).length,
  0,
  "Monaco's vendored sanitizer must not survive in a production chunk"
);
console.log("The production editor uses one maintained DOMPurify implementation.");
