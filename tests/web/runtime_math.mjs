import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { buildRuntimeImports, createRuntimeFilesystem, inspectRuntimeWasmLayout, isSupportedRuntimeImport } from '../../src/web/ide/public/compiler/runtimeImports.js';
const folder = mkdtempSync(path.join(tmpdir(), 'dynlex-wasm-math-'));
try {
  const operations = ['sin', 'cos', 'exp', 'log', 'pow', 'sqrt', 'floor', 'ceil', 'round', 'abs'];
  const lines = [];
  for (const bits of [32, 64]) for (const operation of operations) {
    const value = `@intrinsic("cast", @intrinsic("call", "host", "sample", @intrinsic("type", "float", 64)), @intrinsic("type", "float", ${bits}))`;
    const operands = operation === 'pow' ? `${value}, ${value}` : value;
    lines.push(`@intrinsic("call", "host", "result", @intrinsic("type", "nothing"), @intrinsic("cast", @intrinsic("${operation}", ${operands}), @intrinsic("type", "float", 64)))`);
  }
  const source = path.join(folder, 'math.dl'), output = path.join(folder, 'math.wasm');
  writeFileSync(source, lines.join('\n'));
  for (const optimization of ['-O0', '-O2']) {
    execFileSync(path.resolve(process.argv[2]), [source, '--emit-wasm', optimization, '-o', output]);
    const bytes = readFileSync(output), values = [];
    for (const imported of WebAssembly.Module.imports(new WebAssembly.Module(bytes))) {
      if (!['sample', 'result'].includes(imported.name)) assert(isSupportedRuntimeImport(imported), imported.name);
    }
    const imports = buildRuntimeImports([], createRuntimeFilesystem(), inspectRuntimeWasmLayout(bytes));
    imports.env.sample = () => 1.25;
    imports.env.result = value => values.push(value);
    const { instance } = await WebAssembly.instantiate(bytes, imports);
    instance.exports.main();
    const expected = operations.map(operation => operation === 'pow' ? Math.pow(1.25, 1.25) : (operation === 'round' ? Math.sign(1.25) * Math.floor(Math.abs(1.25) + 0.5) : Math[operation](1.25)));
    assert.deepEqual(values, [...expected.map(Math.fround), ...expected]);
    for (const name of ['fmin', 'fmax', 'fminf', 'fmaxf']) {
      assert.equal(imports.env[name](NaN, 3), 3);
      assert.equal(imports.env[name](3, NaN), 3);
      assert(Number.isNaN(imports.env[name](NaN, NaN)));
    }
    for (const sample of [-1.5, -0.5, -0, 0.5, 1.5]) {
      assert(Object.is(imports.env.round(sample), Math.sign(sample) * Math.floor(Math.abs(sample) + 0.5)));
      assert(Object.is(imports.env.roundf(sample), imports.env.round(sample)));
    }
  }
  console.log('Browser runtime supports float32 and float64 math intrinsics.');
} finally { rmSync(folder, { recursive: true, force: true }); }
