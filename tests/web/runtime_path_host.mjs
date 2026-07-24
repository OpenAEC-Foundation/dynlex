import assert from "node:assert/strict";

import {
  buildRuntimeImports,
  createRuntimeFilesystem,
  isSupportedRuntimeImport
} from "../../src/web/ide/src/worker/runtimeImports.js";

const importNames = [
  "dynlex_host_error_message",
  "dynlex_host_executable_directory",
  "dynlex_host_executable_path",
  "dynlex_host_platform_is_windows",
  "dynlex_host_read_standard_input",
  "dynlex_path_binary",
  "dynlex_path_error_message",
  "dynlex_path_file_uri",
  "dynlex_path_is_absolute",
  "dynlex_path_native_style",
  "dynlex_path_unary"
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
const outputPointer = 8000;
const outputLength = 8004;
let nextInput = 1024;

function writeInput(text) {
  const encoded = encoder.encode(text);
  const pointer = nextInput;
  nextInput += encoded.length + 16;
  new Uint8Array(memory.buffer).set(encoded, pointer);
  return { length: encoded.length, pointer };
}

function readOwnedResult() {
  const view = new DataView(memory.buffer);
  const pointer = view.getUint32(outputPointer, true);
  const length = view.getUint32(outputLength, true);
  const result = decoder.decode(new Uint8Array(memory.buffer).subarray(pointer, pointer + length));
  if (pointer) {
    env.free(pointer);
  }
  return result;
}

function unary(operation, style, text) {
  const input = writeInput(text);
  assert.equal(
    env.dynlex_path_unary(
      operation,
      style,
      input.pointer,
      input.length,
      outputPointer,
      outputLength
    ),
    0
  );
  return readOwnedResult();
}

function binary(operation, style, leftText, rightText) {
  const left = writeInput(leftText);
  const right = writeInput(rightText);
  assert.equal(
    env.dynlex_path_binary(
      operation,
      style,
      left.pointer,
      left.length,
      right.pointer,
      right.length,
      outputPointer,
      outputLength
    ),
    0
  );
  return readOwnedResult();
}

function fileUri(operation, style, text, expectedStatus = 0, expectedSupported = 1) {
  const input = writeInput(text);
  new DataView(memory.buffer).setInt32(8014, 99, true);
  const status = env.dynlex_path_file_uri(
    operation,
    style,
    input.pointer,
    input.length,
    outputPointer,
    outputLength,
    8014
  );
  assert.equal(status, expectedStatus);
  assert.equal(new DataView(memory.buffer).getInt32(8014, true), expectedSupported);
  return status === 0 && expectedSupported ? readOwnedResult() : "";
}

function errorMessage(name) {
  const length = env[name](0, 0);
  assert.equal(length > 0, true);
  env[name](9000, length + 1);
  return decoder.decode(new Uint8Array(memory.buffer).subarray(9000, 9000 + length));
}

new DataView(memory.buffer).setInt32(8014, 99, true);
new DataView(memory.buffer).setInt32(8018, 99, true);
assert.equal(env.dynlex_path_native_style(8014, 8018), 0);
assert.equal(new DataView(memory.buffer).getInt32(8014, true), 0);
assert.equal(new DataView(memory.buffer).getInt32(8018, true), 0);
assert.equal(unary(1, 1, "/alpha//beta/../é"), "/alpha/é");
assert.equal(unary(2, 1, "/alpha/file.txt"), "/alpha");
assert.equal(unary(2, 1, "/alpha/.."), "/alpha");
assert.equal(unary(3, 1, "/alpha/.."), "..");
assert.equal(unary(4, 2, "C:."), ".");
assert.equal(unary(3, 2, "C:\\alpha\\file.txt"), "file.txt");
assert.equal(unary(4, 1, "archive.tar.gz"), "archive.tar");
assert.equal(unary(5, 1, ".."), "");
assert.equal(binary(1, 1, "/alpha/beta", "../gamma"), "/alpha/gamma");
assert.equal(binary(2, 2, "\\asset", "C:/opt/app"), "C:/asset");
assert.equal(binary(3, 2, "C:/OPT/App/File.txt", "c:/opt/app"), "File.txt");

for (const [text, expected] of [
  ["/rooted", 1],
  ["C:/rooted", 1],
  ["//server/share/rooted", 1],
  ["C:relative", 0]
]) {
  const input = writeInput(text);
  assert.equal(env.dynlex_path_is_absolute(2, input.pointer, input.length, 8010), 0);
  assert.equal(new DataView(memory.buffer).getInt32(8010, true), expected);
}

assert.equal(
  fileUri(1, 2, "\\\\sérver\\共享\\文.txt"),
  "file://s%C3%A9rver/%E5%85%B1%E4%BA%AB/%E6%96%87.txt"
);
assert.equal(
  fileUri(2, 2, "file://s%C3%A9rver/%E5%85%B1%E4%BA%AB/%E6%96%87.txt"),
  "//sérver/共享/文.txt"
);
assert.equal(fileUri(2, 1, "file:/tmp/single"), "/tmp/single");
assert.equal(fileUri(2, 1, "file://localhost/tmp/local"), "/tmp/local");
assert.equal(fileUri(2, 1, "file://server/share/file", 0, 0), "");
assert.match(errorMessage("dynlex_path_error_message"), /non-local/i);
assert.equal(fileUri(2, 2, "file:////server/share/file"), "//server/share/file");
assert.equal(fileUri(2, 2, "file://[2001:db8::1]/share/file"), "//[2001:db8::1]/share/file");
assert.equal(fileUri(1, 1, "/tmp/a/../target"), "file:///tmp/a/../target");
assert.equal(fileUri(2, 1, "file:///tmp/a/%2E%2E/target"), "/tmp/a/../target");

for (const invalid of [
  "file:///tmp/a%2Fb",
  "file:///tmp/%00",
  "file:///tmp/%FF",
  "file:///tmp/raw space"
]) {
  fileUri(2, 1, invalid, -1);
  assert.equal(new DataView(memory.buffer).getUint32(outputPointer, true), 0);
  assert.equal(new DataView(memory.buffer).getUint32(outputLength, true), 0);
  assert.equal(errorMessage("dynlex_path_error_message").length > 0, true);
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

const view = new DataView(memory.buffer);
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

console.log("Browser path and host imports passed");
