#!/usr/bin/env node
// Execute a trusted build-time DynLex program with the normal WebAssembly runtime.
import { readFile } from 'node:fs/promises';
import { buildRuntimeImports, createRuntimeFilesystem, inspectRuntimeWasmLayout } from '../src/web/ide/public/compiler/runtimeImports.js';

const bytes = await readFile(process.argv[2]);
const output = { push(chunk) { process.stdout.write(chunk); } };
const runtime = buildRuntimeImports(output, createRuntimeFilesystem(), inspectRuntimeWasmLayout(bytes));
const { instance } = await WebAssembly.instantiate(bytes, runtime);
instance.exports.main();
