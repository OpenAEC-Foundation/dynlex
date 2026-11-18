#!/usr/bin/env node
// Exercise the shared editor compiler through its actual WebAssembly host API.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";

assert.equal(process.argv.length, 3, "usage: node scripts/test_web_lsp.mjs build-web/dynlex_web.js");
const modulePath = path.resolve(process.argv[2]);
const createModule = (await import(pathToFileURL(modulePath).href)).default;
const module = await createModule({
  wasmBinary: await readFile(path.join(path.dirname(modulePath), "dynlex_web.wasm")),
  locateFile: name => path.join(path.dirname(modulePath), name),
});
const uri = "file:///workspace/incremental.dl";
let id = 0;
let version = 1;
let notifications = [];
function send(method, params, request = false) {
  const message = { jsonrpc: "2.0", method, params };
  if (request) message.id = ++id;
  const messages = JSON.parse(module.ccall("dynlex_web_lsp_exchange_json", "string", ["string"], [JSON.stringify(message)]));
  notifications.push(...messages.filter(value => value.method));
  assert(!messages.some(value => value.error), JSON.stringify(messages));
  if (request) return messages.find(value => value.id === id).result;
}
function initialize(text) {
  module.ccall("dynlex_web_init", null, [], []);
  notifications = [];
  send("initialize", { rootUri: "file:///workspace", capabilities: {}, initializationOptions: { dynlex: {
    analysisProfiles: [{ target: "cpu" }, { target: "spirv", shaderStage: "fragment" }],
  } } }, true);
  send("initialized", {});
  send("textDocument/didOpen", { textDocument: { uri, languageId: "dynlex", version: 1, text } });
}
function result() {
  const tokens = send("textDocument/semanticTokens/full", { textDocument: { uri } }, true);
  const reports = notifications.filter(value => value.method === "textDocument/publishDiagnostics" && value.params.uri === uri);
  assert(reports.length);
  return { tokens, diagnostics: reports.at(-1).params.diagnostics };
}
function edit(text, error = false) {
  notifications = [];
  send("textDocument/didChange", { textDocument: { uri, version: ++version }, contentChanges: [{ text }] });
  const actual = result();
  assert.equal(actual.diagnostics.some(value => value.severity === 1), error, JSON.stringify(actual.diagnostics));
  initialize(text);
  assert.deepEqual(actual, result(), "WebAssembly editor results differ from fresh analysis");
}
const source = 'to stable {value}: @intrinsic("discard", value)\nto edited {value}: @intrinsic("discard", value)\nstable 1\nedited 2\n';
initialize(source);
assert.equal(result().diagnostics.length, 0);
edit(source.replace("stable 1\n", "stable 1 # a longer café 🧪 comment\n"));
edit(source);
const edited = source.replace('to edited {value}: @intrinsic("discard", value)',
                              'to edited {value}: @intrinsic("discard", @intrinsic("add", value, 1))');
edit(edited);
edit(edited + 'to stable 1: @intrinsic("discard", 9)\n');
edit(edited + 'mystery 3\n', true);
edit(edited + 'mystery 3\nto mystery {value}: @intrinsic("discard", value)\n');
const implicit = 'to implicit item: @intrinsic("discard", item)\nimplicit 2\n';
edit(edited + implicit);
edit(edited + implicit.replace('"discard", item', '"discard", 0'), true);
edit(edited + implicit);
send("textDocument/didClose", { textDocument: { uri } });
console.log("WebAssembly LSP edits preserve overloads, failed lookups, classification, profiles and fresh-analysis parity.");
