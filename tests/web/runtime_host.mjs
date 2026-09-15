import assert from "node:assert/strict";

import {
  buildRuntimeImports,
  createRuntimeFilesystem,
  isSupportedRuntimeImport
} from "../../src/web/ide/public/compiler/runtimeImports.js";

const importNames = [
  "dynlex_host_error_message",
  "dynlex_host_user_cache_directory",
  "dynlex_host_executable_directory",
  "dynlex_host_executable_path",
  "dynlex_host_environment_value",
  "dynlex_host_find_executable",
  "dynlex_host_exit",
  "dynlex_host_is_administrator",
  "dynlex_host_platform_name",
  "dynlex_host_platform_is_windows",
  "dynlex_host_read_standard_input",
  "dynlex_host_write_standard_error",
];
for (const name of importNames) {
  assert.equal(isSupportedRuntimeImport({ module: "env", name }), true, `${name} must be supported`);
}

const { env } = buildRuntimeImports(
  importNames.map((name) => ({ module: "env", name })),
  [],
  createRuntimeFilesystem(),
  { staticDataEnd: 0 }
);
const memory = env.__linear_memory;
const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8", { fatal: true });
let nextInput = 1024;

function writeInput(text) {
  const encoded = encoder.encode(text);
  const pointer = nextInput;
  nextInput += encoded.length + 16;
  new Uint8Array(memory.buffer).set(encoded, pointer);
  return { length: encoded.length, pointer };
}

function errorMessage(name) {
  const length = env[name](0, 0);
  assert.equal(length > 0, true);
  env[name](9000, length + 1);
  return decoder.decode(new Uint8Array(memory.buffer).subarray(9000, 9000 + length));
}

new DataView(memory.buffer).setInt32(8014, 99, true);
new DataView(memory.buffer).setInt32(8018, 99, true);
assert.equal(env.dynlex_host_platform_is_windows(8014, 8018), 0);
assert.equal(new DataView(memory.buffer).getInt32(8014, true), 0);
assert.equal(new DataView(memory.buffer).getInt32(8018, true), 0);
new DataView(memory.buffer).setUint32(8020, 99, true);
new DataView(memory.buffer).setInt32(8024, 99, true);
assert.equal(env.dynlex_host_executable_path(0, 0, 8020, 8024), 0);
assert.equal(new DataView(memory.buffer).getUint32(8020, true), 0);
assert.equal(new DataView(memory.buffer).getInt32(8024, true), 0);
assert.match(errorMessage("dynlex_host_error_message"), /browser/i);
assert.equal(env.dynlex_host_executable_directory(0, 0, 8020, 8024), 0);
assert.equal(env.dynlex_host_user_cache_directory(0, 0, 8020, 8024), 0);
assert.equal(new DataView(memory.buffer).getUint32(8020, true), 0);
assert.equal(new DataView(memory.buffer).getInt32(8024, true), 0);
assert.match(errorMessage("dynlex_host_error_message"), /browser/i);

const view = new DataView(memory.buffer);
const hostNameLength = 8050;
assert.equal(env.dynlex_host_platform_name(0, 0, hostNameLength), 0);
assert.equal(view.getUint32(hostNameLength, true), 7);
assert.equal(env.dynlex_host_platform_name(8060, 7, hostNameLength), 0);
assert.equal(decoder.decode(new Uint8Array(memory.buffer).subarray(8060, 8067)), "Browser");
assert.equal(env.dynlex_host_is_administrator(8070, 8078), 0);
assert.equal(view.getInt32(8070, true), 0);
assert.equal(view.getInt32(8078, true), 0);
const lookup = writeInput("PATH");
assert.equal(env.dynlex_host_environment_value(lookup.pointer, lookup.length, 0, 0, 8074, 8078, 8082), 0);
assert.equal(view.getUint32(8074, true), 0);
assert.equal(view.getInt32(8078, true), 0);
assert.equal(view.getInt32(8082, true), 0);
assert.match(errorMessage("dynlex_host_error_message"), /environment/i);
assert.equal(env.dynlex_host_find_executable(lookup.pointer, lookup.length, 0, 0, 8074, 8078, 8082), 0);
assert.match(errorMessage("dynlex_host_error_message"), /executable/i);
assert.equal(env.dynlex_host_write_standard_error(lookup.pointer, lookup.length, 8082), 0);
assert.match(errorMessage("dynlex_host_error_message"), /standard error/i);
assert.throws(() => env.dynlex_host_exit(7), /status 7/i);

view.setUint32(8030, 99, true);
view.setUint32(8034, 99, true);
view.setInt32(8038, 99, true);
view.setInt32(8042, 99, true);
assert.equal(env.dynlex_host_read_standard_input(8030, 8034, 8038, 8042), 0);
assert.equal(view.getUint32(8030, true), 0);
assert.equal(view.getUint32(8034, true), 0);
assert.equal(view.getInt32(8038, true), 0);
assert.equal(view.getInt32(8042, true), 0);
assert.match(errorMessage("dynlex_host_error_message"), /standard input/i);

console.log("Browser host imports passed");
