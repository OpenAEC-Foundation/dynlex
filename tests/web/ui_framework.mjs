import assert from 'node:assert/strict';
import path from 'node:path';
import { readFile } from 'node:fs/promises';
import { pathToFileURL } from 'node:url';
import { buildRuntimeImports, createRuntimeFilesystem, inspectRuntimeWasmLayout } from '../../src/web/ide/public/compiler/runtimeImports.js';

const directory = path.resolve(process.argv[2] || 'build-web');
const createModule = (await import(pathToFileURL(path.join(directory, 'dynlex_web.js')).href)).default;
const compiler = await createModule({ locateFile: name => path.join(directory, name) });
compiler.ccall('dynlex_web_init', null, [], []);
function compile(source, success = true) {
  compiler.ccall('dynlex_web_set_main_source', null, ['string'], [source]);
  const status = compiler.ccall('dynlex_web_compile_and_emit_wasm', 'number', [], []);
  const diagnostics = compiler.ccall('dynlex_web_get_diagnostics_json', 'string', [], []);
  if (!success) { assert.notEqual(status, 0, 'The extracted UI must not remain embedded in the compiler'); return diagnostics; }
  assert.equal(status, 0, diagnostics);
  assert.equal(JSON.parse(diagnostics).diagnostics.length, 0, diagnostics);
  const encoded = compiler.ccall('dynlex_web_get_output_wasm_base64', 'string', [], []);
  return Buffer.from(encoded, 'base64');
}
async function run(source, abort = false) {
  const bytes = compile(source), output = [];
  const imports = buildRuntimeImports(output, createRuntimeFilesystem(), inspectRuntimeWasmLayout(bytes));
  const { instance } = await WebAssembly.instantiate(bytes, imports);
  assert(instance.exports.__indirect_function_table instanceof WebAssembly.Table);
  if (abort) assert.throws(() => instance.exports.main(), /Program aborted/);
  else assert.equal(instance.exports.main(), 0);
  return output.join('');
}
for (const fixture of ['static_callback_cast', 'recursive_local_comparison']) {
  const source = await readFile(new URL(`../required/${fixture}/main.dl`, import.meta.url), 'utf8');
  assert.equal(await run(source), '');
}
assert.equal(await run('import lib/utf8.dl\nprint line UTF8 text for code point 233\n'), 'é\n');
assert.equal(await run('import lib/utf8.dl\nprint line the previous UTF8 boundary before 2 in (the string form of "A")\n', true),
  'A UTF-8 byte offset must be inside the text\n');
assert.match(compile('import lib/ui.dl\n', false), /ui\.dl/);
console.log('Browser compiler: static callbacks, recursion, current UTF-8 library and extracted UI verified.');
