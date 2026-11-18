#!/usr/bin/env node
// Execute a trusted build-time DynLex program with the normal WebAssembly runtime.
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { buildRuntimeImports, createRuntimeFilesystem, inspectRuntimeWasmLayout } from '../src/web/ide/public/compiler/runtimeImports.js';

const bytes = await readFile(process.argv[2]);
const output = { push(chunk) { process.stdout.write(chunk); } };
const filesystem = createRuntimeFilesystem();
if (process.argv[3]) {
  const files = JSON.parse(await readFile(process.argv[3], 'utf8'));
  if (files === null || typeof files !== 'object' || Array.isArray(files)) throw new Error('Runtime files must be an object');
  for (const [name, text] of Object.entries(files)) {
    if (!path.posix.isAbsolute(name) || path.posix.normalize(name) !== name || name.endsWith('/') ||
        name.includes('\0') || typeof text !== 'string') throw new Error('A runtime file needs a normalized absolute path and text');
    filesystem.files.set(name, { data: new TextEncoder().encode(text), modificationTime: filesystem.lastTimestamp });
    for (let directory = path.posix.dirname(name); directory !== '/'; directory = path.posix.dirname(directory))
      filesystem.directories.set(directory, filesystem.lastTimestamp);
  }
}
const runtime = buildRuntimeImports(output, filesystem, inspectRuntimeWasmLayout(bytes));
const { instance } = await WebAssembly.instantiate(bytes, runtime);
instance.exports.main();
