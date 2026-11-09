import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { buildRuntimeImports, createRuntimeFilesystem, inspectRuntimeWasmLayout } from "../../src/web/ide/public/compiler/runtimeImports.js";

const compiler = path.resolve(process.argv[2]);
const directory = mkdtempSync(path.join(tmpdir(), "dynlex-host-callbacks-"));
try {
  const source = path.join(directory, "callbacks.dl");
  const output = path.join(directory, "callbacks.wasm");
  writeFileSync(source, `integer32 means: @intrinsic("type", "int", 32)
address means: @intrinsic("add pointer depth", integer32)
meta means: @intrinsic("type", "type")
class:
    patterns:
        callback table
    members:
        entry as address
function value as {meta:destination}:
    execute:
        @intrinsic("return", @intrinsic("cast", value, destination))
function incremented {integer32:value}:
    execute:
        @intrinsic("return", @intrinsic("add", value, 1))
@intrinsic("store", callbacks, @intrinsic("static reference", @intrinsic("construct", callback table, @intrinsic("function", "incremented $") as address)))
@intrinsic("call", "host", "save_callback", @intrinsic("type", "nothing"), callbacks' entry)
@intrinsic("call", "host", "save_result", @intrinsic("type", "nothing"), @intrinsic("call pointer", @intrinsic("function", "incremented $"), integer32, 8))
@intrinsic("call", "host", "save_result", @intrinsic("type", "nothing"), @intrinsic("invoke", @intrinsic("function", "incremented $"), 9))
`);
  for (const optimization of ["-O0", "-O2"]) {
    execFileSync(compiler, [source, "--emit-wasm", optimization, "-o", output]);
    const bytes = readFileSync(output);
    const imports = buildRuntimeImports([], createRuntimeFilesystem(), inspectRuntimeWasmLayout(bytes));
    let callback;
    const results = [];
    imports.env.save_callback = pointer => { callback = pointer; };
    imports.env.save_result = value => results.push(value);
    const { instance } = await WebAssembly.instantiate(bytes, imports);
    instance.exports.main();
    assert.ok(instance.exports.__indirect_function_table instanceof WebAssembly.Table,
      "The host must be able to invoke function pointers received through imports");
    assert.equal(instance.exports.__indirect_function_table.get(callback)(41), 42);
    assert.deepEqual(results, [9, 10]);
  }
  console.log("WebAssembly hosts can invoke typed DynLex callbacks after main returns.");
} finally {
  rmSync(directory, { recursive: true, force: true });
}
