import assert from 'node:assert/strict';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { buildRuntimeImports, createRuntimeFilesystem, inspectRuntimeWasmLayout } from '../../src/web/ide/public/compiler/runtimeImports.js';

const directory = path.resolve(process.argv[2] || 'src/web/ide/public/compiler');
const createModule = (await import(pathToFileURL(path.join(directory, 'dynlex_web.js')).href)).default;
const compiler = await createModule({ locateFile: name => path.join(directory, name) });
const call = (name, result = null, types = [], values = []) => compiler.ccall(name, result, types, values);
call('dynlex_web_init');
const sources = files => call('dynlex_web_set_import_sources', 'number', ['string'], [JSON.stringify(files)]);
const main = 'import packages/example/entry.dl\n@intrinsic("call", "host", "capture", @intrinsic("type", "nothing"), doubled 21)\n';
call('dynlex_web_set_main_source', null, ['string'], [main]);
const helpers = value => ({
  'packages/example/entry.dl': 'import nested/helpers.dl\n',
  'packages/example/nested/helpers.dl': 'number means: @intrinsic("type", "int", 32)\ndoubled {number:value} means: @intrinsic("multiply", value, ' + value + ')\n'
});
async function run(expected) {
  assert.equal(call('dynlex_web_compile_and_emit_wasm', 'number'), 0,
    call('dynlex_web_get_diagnostics_json', 'string'));
  const bytes = Buffer.from(call('dynlex_web_get_output_wasm_base64', 'string'), 'base64');
  const imports = buildRuntimeImports([], createRuntimeFilesystem(), inspectRuntimeWasmLayout(bytes));
  let value;
  imports.env.capture = result => { value = result; };
  const { instance } = await WebAssembly.instantiate(bytes, imports);
  instance.exports.main();
  assert.equal(value, expected);
}
assert.equal(sources(helpers(2)), 0);
await run(42);
assert.equal(sources(helpers(3)), 0);
await run(63);
assert.equal(sources({ ...helpers(4), 'app/main.dl': main.replace('packages/', '../packages/') }), 0);
assert.equal(call('dynlex_web_set_entry_file', 'number', ['string'], ['app/main.dl']), 0);
await run(84);
assert.equal(sources({ ...helpers(3), 'main.dl': main }), 0);
assert.equal(call('dynlex_web_set_entry_file', 'number', ['string'], ['main.dl']), 0);
await run(63);
assert.notEqual(call('dynlex_web_set_entry_file', 'number', ['string'], ['../outside.dl']), 0);
call('dynlex_web_set_main_source', null, ['string'], [main]);
await run(63);
assert.equal(sources({}), 0);
assert.notEqual(call('dynlex_web_compile_and_emit_wasm', 'number'), 0, 'Removed dependencies must not remain available');
assert.match(call('dynlex_web_get_diagnostics_json', 'string'), /entry\.dl/);
for (const files of [{ '../outside.dl': '' }, { '/lib/override.dl': '' }, { 'bad.dl': 3 }]) {
  assert.notEqual(sources(files), 0, 'Invalid source snapshots must fail explicitly');
}
assert.notEqual(call('dynlex_web_set_import_sources', 'number', ['string'], [null]), 0);
assert.equal(sources(helpers(2)), 0);
call('dynlex_web_init');
call('dynlex_web_set_main_source', null, ['string'], [main]);
assert.notEqual(call('dynlex_web_compile_and_emit_wasm', 'number'), 0, 'Reinitializing the compiler must clear imported project sources');
console.log('Browser project sources: nested imports, file entry points, replacement, deletion, validation and reset passed.');
