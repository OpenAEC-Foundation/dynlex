import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import {
  buildRuntimeImports,
  createRuntimeFilesystem,
  inspectRuntimeWasmLayout,
  isSupportedRuntimeImport
} from "../../src/web/ide/public/compiler/runtimeImports.js";

const compiler = path.resolve(process.argv[2]);
const temporary = mkdtempSync(path.join(tmpdir(), "dynlex-wasm-layout-"));
const nullComparison = `to get pointer is null:
    @intrinsic("return", @intrinsic("equal", pointer, @intrinsic("cast", 0, @intrinsic("type", "string"))))
@intrinsic("return", "x" is null)
`;
const cases = [
  { name: "first-literal", source: nullComparison, exit: 0, output: "" },
  { name: "shifted-literal", source: `@intrinsic("discard", "padding")\n${nullComparison}`, exit: 0, output: "" },
  { name: "empty-literal", source: nullComparison.replace('"x" is null', '"" is null'), exit: 0, output: "" },
  { name: "literal-byte", source: '@intrinsic("return", @intrinsic("cast", @intrinsic("dereference", "x"), @intrinsic("type", "int", 32)))\n', exit: 120, output: "" },
  { name: "strings-and-stack", source: `import lib/std.dl
to get the text at index in values:
    return the item at index in values
set values to ["x", "yz", "last"]
print line the text at 0 in values
print line the text at 1 in values
print line the text at 2 in values
set text to the string form of "heap"
print line text
`, exit: 0, output: "x\nyz\nlast\nheap\n" },
  { name: "global-string", source: `import lib/std.dl
function print saved text:
    globals: saved
    execute:
        print line saved
set saved to "first"
print saved text
set saved to "second"
print saved text
`, exit: 0, output: "first\nsecond\n" }
];

try {
  for (const optimization of ["-O0", "-O2"]) {
    for (const test of cases) {
      const source = path.join(temporary, `${test.name}.dl`);
      const outputPath = path.join(temporary, `${test.name}.wasm`);
      writeFileSync(source, test.source);
      execFileSync(compiler, [source, "--emit-wasm", optimization, "-o", outputPath]);
      const bytes = readFileSync(outputPath);
      const module = await WebAssembly.compile(bytes);
      const specifications = WebAssembly.Module.imports(module);
      assert.ok(specifications.every(isSupportedRuntimeImport), `${test.name}: unsupported import`);
      const output = [];
      const layout = inspectRuntimeWasmLayout(bytes);
      const imports = buildRuntimeImports(output, createRuntimeFilesystem(), layout);
      const instance = await WebAssembly.instantiate(module, imports);
      assert.equal(instance.exports.main(), test.exit, `${optimization} ${test.name}: exit`);
      assert.equal(output.join(""), test.output, `${optimization} ${test.name}: output`);
      assert.equal(layout.heapBase, instance.exports.__heap_base.value, "runtime must use the linked heap base");
    }
  }
  console.log("WebAssembly memory layout regressions passed");
} finally {
  rmSync(temporary, { recursive: true, force: true });
}
