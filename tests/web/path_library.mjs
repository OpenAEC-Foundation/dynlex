import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import {
  buildRuntimeImports,
  createRuntimeFilesystem,
  inspectRuntimeWasmLayout,
  isSupportedRuntimeImport
} from "../../src/web/ide/public/compiler/runtimeImports.js";

const project = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const compiler = path.resolve(process.argv[2]);
const temporary = mkdtempSync(path.join(tmpdir(), "dynlex-path-wasm-"));
try {
  for (const name of ["path_library", "path_uri", "path_uri_edge_cases"]) {
    const source = path.join(project, "tests/required", name, "main.dl");
    const wasm = path.join(temporary, `${name}.wasm`);
    const diagnostics = execFileSync(compiler, [source, "--emit-wasm", "-o", wasm], { cwd: project, encoding: "utf8" });
    assert.equal(diagnostics, "");
    const bytes = readFileSync(wasm);
    const module = await WebAssembly.compile(bytes);
    const specifications = WebAssembly.Module.imports(module);
    assert.ok(specifications.every(isSupportedRuntimeImport), `${name}: unsupported runtime import`);
    assert.ok(specifications.every(({ name }) => !name.startsWith("dynlex_path_")), "Paths must execute in DynLex");
    const output = [];
    const imports = buildRuntimeImports(specifications, output, createRuntimeFilesystem(), inspectRuntimeWasmLayout(bytes));
    const instance = await WebAssembly.instantiate(module, imports);
    instance.exports.main();
    const expected = readFileSync(path.join(project, "tests/required", name, "expected.txt"), "utf8");
    assert.equal(output.join("").trim(), expected.trim(), name);
    console.log(`${name}: compiled DynLex WebAssembly passed`);
  }
} finally {
  rmSync(temporary, { recursive: true, force: true });
}
