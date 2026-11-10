import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { buildRuntimeImports, createRuntimeFilesystem, inspectRuntimeWasmLayout, isSupportedRuntimeImport } from "../../src/web/ide/public/compiler/runtimeImports.js";

const folder = mkdtempSync(path.join(tmpdir(), "dynlex-wide-integers-"));
const lines = [
  "import lib/std.dl",
  "square value means: value * value",
  "known {fixed a value:value} means: value",
  'print line formatted "{square square square square square (2 as a 128 bit integer)}"'
];
lines.push("print line known 18446744073709551616");
const expected = ["4294967296", "18446744073709551616"];
for (const width of [72, 128, 256, 512]) {
  const high = (1n << BigInt(width - 3)) * 3n;
  lines.push(`set sample${width} to @intrinsic("call", "host", "sample", a 64 bit integer) as a ${width} bit integer`);
  lines.push(`set high${width} to sample${width} << ${width - 3}`);
  lines.push(`print line formatted "{high${width}} {-high${width}} {high${width} / sample${width}} {high${width} mod (7 as a ${width} bit integer)}"`);
  lines.push(`print line (high${width} as a 64 bit floating-point number) as a ${width} bit integer`);
  lines.push(`print line known (((1 as a ${width} bit integer) << ${width - 3}) * 3)`);
  expected.push(`${high} ${-high} ${high / 3n} ${high % 7n}`, String(high), String(high));
}
lines.push('print line formatted "{sample128 * sample128} {sample128 << sample128} {(-sample128) >> sample128}"');
expected.push("9 24 -1");
lines.push("set unsignedSample to sample128 as a 128 bit unsigned integer");
lines.push('set maximum to @intrinsic("bitwise not", unsignedSample - unsignedSample)');
lines.push('print line formatted "{maximum} {maximum / unsignedSample} {maximum mod unsignedSample}"');
const maximum = (1n << 128n) - 1n;
expected.push(`${maximum} ${maximum / 3n} ${maximum % 3n}`);
lines.push("print line (((-sample128) as a 128 bit unsigned integer) >> unsignedSample)");
expected.push(String((1n << 125n) - 1n));
lines.push("print line ((high128 as a 128 bit unsigned integer) as a 64 bit floating-point number) as a 128 bit unsigned integer");
expected.push(String(3n << 125n));
lines.push("print line ((-high128) as a 64 bit floating-point number) as a 128 bit integer");
expected.push(String(-(3n << 125n)));
lines.push("print line sample128 << 127");
expected.push(String(-(1n << 127n)));
lines.push("print line known ((1267650675786093127411026624513 as a 32 bit floating-point number) as a 128 bit integer)");
expected.push(String((1n << 100n) + (1n << 77n)));
lines.push("print line known (((2 as a 128 bit integer) as a 64 bit floating-point number) as a 128 bit integer)");
expected.push("2");
lines.push("print line the count of decimal digits in (0 as a 128 bit unsigned integer)");
expected.push("1");
lines.push("set empty to 7 padded to 0 digits");
lines.push('print line formatted "left{empty}right"');
expected.push("leftright");
// An i128 -> f32 conversion must round once, even just above a halfway value
// that would disappear if the host rounded through f64 first.
lines.push('set rounded to @intrinsic("call", "host", "rounding sample", a 128 bit integer)');
lines.push('print line (rounded as a 32 bit floating-point number) as a 128 bit integer');
expected.push(String((1n << 100n) + (1n << 77n)));
const source = lines.join("\n") + "\n";
const output = expected.join("\n") + "\n";

async function execute(bytes) {
  const module = new WebAssembly.Module(bytes);
  for (const imported of WebAssembly.Module.imports(module)) {
    if (!["sample", "rounding sample"].includes(imported.name)) assert(isSupportedRuntimeImport(imported), imported.name);
  }
  const chunks = [];
  const imports = buildRuntimeImports(chunks, createRuntimeFilesystem(), inspectRuntimeWasmLayout(bytes));
  imports.env.sample = () => 3n;
  imports.env["rounding sample"] = (pointer) => {
    const value = (1n << 100n) + (1n << 76n) + 1n;
    const view = new DataView(imports.env.__linear_memory.buffer);
    view.setBigUint64(pointer, BigInt.asUintN(64, value), true);
    view.setBigUint64(pointer + 8, value >> 64n, true);
  };
  const instance = await WebAssembly.instantiate(module, imports);
  assert.equal(instance.exports.main(), 0);
  assert.equal(chunks.join(""), output);
}

try {
  const input = path.join(folder, "main.dl"), wasm = path.join(folder, "main.wasm");
  writeFileSync(input, source);
  for (const optimization of ["-O0", "-O2"]) {
    execFileSync(path.resolve(process.argv[2]), [input, "--emit-wasm", optimization, "-o", wasm], { timeout: 30000 });
    await execute(readFileSync(wasm));
  }
  if (process.argv[3]) {
    const buildDir = path.resolve(process.argv[3]);
    const imported = await import(pathToFileURL(path.join(buildDir, "dynlex_web.js")));
    const compiler = await imported.default({ locateFile: name => path.join(buildDir, name) });
    compiler.ccall("dynlex_web_init", null, [], []);
    compiler.ccall("dynlex_web_set_main_source", null, ["string"], [source]);
    const status = compiler.ccall("dynlex_web_compile_and_emit_wasm", "number", [], []);
    assert.equal(status, 0, compiler.ccall("dynlex_web_get_diagnostics_json", "string", [], []));
    await execute(Buffer.from(compiler.ccall("dynlex_web_get_output_wasm_base64", "string", [], []), "base64"));
  }
  console.log("Wide integers retain exact arithmetic, formatting and casts in WebAssembly.");
} finally {
  rmSync(folder, { recursive: true, force: true });
}
