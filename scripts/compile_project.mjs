// Compile source snapshots with the compiler shipped to browser projects.
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { createWgslTranslator } from '../web/wgsl-translator.js';

const [requestFile, outputArgument, compilerArgument = 'src/web/ide/public/compiler'] = process.argv.slice(2);
if (!requestFile || !outputArgument) throw new Error('Usage: compile_project.mjs <project.json> <output> [compiler directory]');
const request = JSON.parse(await readFile(requestFile, 'utf8'));
const directory = path.resolve(compilerArgument);
const factory = (await import(pathToFileURL(path.join(directory, 'dynlex_web.js')).href)).default;
const compiler = await factory({ locateFile: name => path.join(directory, name) });
const call = (name, result = null, types = [], values = []) => compiler.ccall(name, result, types, values);
const json = name => JSON.parse(call(name, 'string'));
function checked(name, types = [], values = []) {
  if (call(name, 'number', types, values) !== 0)
    throw new Error(JSON.stringify({ diagnostics: json('dynlex_web_get_diagnostics_json'),
      log: json('dynlex_web_get_compiler_log_json') }));
}
call('dynlex_web_init');
checked('dynlex_web_set_import_sources', ['string'], [JSON.stringify(request.sources)]);
const jobs = request.jobs ?? [{ entry: request.entry, kind: 'wasm', output: path.basename(outputArgument) }];
const output = request.jobs ? path.resolve(outputArgument) : path.dirname(path.resolve(outputArgument));
let translator;
for (const job of jobs) {
  checked('dynlex_web_set_entry_file', ['string'], [job.entry]);
  const target = path.join(output, job.output);
  await mkdir(path.dirname(target), { recursive: true });
  if (job.kind === 'wasm') {
    checked(job.traced ? 'dynlex_web_compile_and_emit_wasm_traced' : 'dynlex_web_compile_and_emit_wasm');
    await writeFile(target, Buffer.from(call('dynlex_web_get_output_wasm_base64', 'string'), 'base64'));
  } else if (job.kind === 'shader') {
    translator ??= await createWgslTranslator(await readFile(path.join(directory, 'dynlex_wgsl_translator.wasm')));
    checked('dynlex_web_compile_and_emit_shader_spirv', ['string'], [job.stage]);
    const bytes = Buffer.from(call('dynlex_web_get_output_shader_spirv_base64', 'string'), 'base64');
    await writeFile(target + '.wgsl', translator.translate(bytes));
    await writeFile(target + '.glsl', translator.translateGlsl(bytes, job.stage));
    await writeFile(target + '.json', JSON.stringify(json('dynlex_web_get_shader_uniforms_json')) + '\n');
  } else throw new Error(`Unknown output kind: ${job.kind}`);
  console.log(`Compiled ${job.entry} → ${job.output}`);
}
