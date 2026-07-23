import assert from "node:assert/strict";

import {
  buildRuntimeImports,
  createRuntimeFilesystem,
  isSupportedRuntimeImport
} from "../../src/web/ide/src/worker/runtimeImports.js";

const filesystemImportNames = [
  "abort",
  "dynlex_filesystem_clear_error",
  "dynlex_filesystem_create_directory",
  "dynlex_filesystem_error_message",
  "dynlex_filesystem_remove",
  "dynlex_filesystem_status",
  "fclose",
  "ferror",
  "fflush",
  "fgetc",
  "fopen",
  "fread",
  "fwrite",
  "memchr",
  "realloc",
  "remove",
  "rename",
  "rewind",
  "tmpfile"
];
const importSpecs = filesystemImportNames.map((name) => ({ module: "env", name }));
for (const importSpec of importSpecs) {
  assert.equal(isSupportedRuntimeImport(importSpec), true, `${importSpec.name} must be supported`);
}
assert.equal(isSupportedRuntimeImport({ module: "env", name: "unknown" }), false);

const filesystem = createRuntimeFilesystem();
const emptyLayout = { staticDataEnd: 0 };
const { env } = buildRuntimeImports(importSpecs, [], filesystem, emptyLayout);
const memory = env.__linear_memory;
let bytes = new Uint8Array(memory.buffer);
const encoder = new TextEncoder();

function writeCString(pointer, text) {
  const encoded = encoder.encode(text);
  bytes.set(encoded, pointer);
  bytes[pointer + encoded.length] = 0;
}

writeCString(1024, "a.bin");
writeCString(1056, "b.bin");
writeCString(1088, "wb");
writeCString(1096, "rb");
writeCString(1104, "ab");

bytes.set([0x80, 0], 1136);
bytes.set([0x81, 0], 1140);
bytes.set([21, 22], 2060);
let stream = env.fopen(1136, 1088);
assert.notEqual(stream, 0);
assert.equal(env.fwrite(2060, 1, 1, stream), 1);
assert.equal(env.fclose(stream), 0);
stream = env.fopen(1140, 1088);
assert.notEqual(stream, 0);
assert.equal(env.fwrite(2061, 1, 1, stream), 1);
assert.equal(env.fclose(stream), 0);
stream = env.fopen(1136, 1096);
assert.notEqual(stream, 0);
assert.equal(env.fread(2064, 1, 1, stream), 1);
assert.equal(bytes[2064], 21, "filesystem keys must preserve non-UTF-8 path bytes");
assert.equal(env.fclose(stream), 0);

bytes.set([65, 0, 66], 2048);
stream = env.fopen(1024, 1088);
assert.notEqual(stream, 0);
assert.equal(env.fwrite(2048, 1, 3, stream), 3);
assert.equal(env.fflush(stream), 0);
assert.equal(env.fclose(stream), 0);

bytes[2051] = 67;
stream = env.fopen(1024, 1104);
assert.notEqual(stream, 0);
assert.equal(env.fwrite(2051, 1, 1, stream), 1);
assert.equal(env.fclose(stream), 0);

stream = env.fopen(1024, 1096);
assert.notEqual(stream, 0);
assert.equal(env.fread(3072, 1, 4, stream), 4);
assert.deepEqual([...bytes.subarray(3072, 3076)], [65, 0, 66, 67]);
assert.equal(env.fgetc(stream), -1);
assert.equal(env.ferror(stream), 0);
assert.equal(env.fclose(stream), 0);

assert.equal(env.rename(1024, 1056), 0);
assert.equal(env.fopen(1024, 1096), 0);
assert.notEqual(env.fopen(1056, 1096), 0);
assert.equal(env.remove(1056), 0);
assert.equal(env.remove(1056), -1);

writeCString(1160, "metadata-directory");
writeCString(1200, "metadata-directory/file.bin");
writeCString(1240, "missing/child");
writeCString(1270, "missing/file.bin");
writeCString(1300, "metadata-directory/subtree");
writeCString(1340, "metadata-directory/moved.bin");
assert.equal(env.dynlex_filesystem_create_directory(1240), -1, "a directory requires an existing parent");
assert.equal(env.fopen(1270, 1088), 0, "a file requires an existing parent");
assert.equal(env.dynlex_filesystem_create_directory(1160), 0);
assert.equal(env.dynlex_filesystem_status(1160, 6000, 6008), 0);
let view = new DataView(memory.buffer);
assert.equal(view.getInt32(6000, true), 0);
const directoryCreationTime = view.getBigInt64(6008, true);
assert.equal(directoryCreationTime > 0n, true);
assert.equal(env.fopen(1160, 1096), 0, "directories must not open as regular files");

stream = env.fopen(1200, 1088);
assert.notEqual(stream, 0);
assert.equal(env.fwrite(2060, 1, 2, stream), 2);
assert.equal(env.fclose(stream), 0);
assert.equal(env.dynlex_filesystem_status(1160, 6000, 6008), 0);
const directoryAfterFileCreation = new DataView(memory.buffer).getBigInt64(6008, true);
assert.equal(directoryAfterFileCreation > directoryCreationTime, true);
assert.equal(env.dynlex_filesystem_status(1200, 6000, 6008), 0);
view = new DataView(memory.buffer);
assert.equal(view.getInt32(6000, true), 1);
assert.equal(view.getBigInt64(6008, true) > 0n, true);
assert.equal(env.rename(1200, 1270), -1, "rename requires an existing destination parent");
assert.equal(env.dynlex_filesystem_status(1200, 6000, 6008), 0, "failed rename must preserve the source");
assert.equal(env.rename(1160, 1300), -1, "a directory cannot be renamed into its own subtree");
assert.equal(env.rename(1200, 1340), 0);
assert.equal(env.dynlex_filesystem_status(1160, 6000, 6008), 0);
const directoryAfterRename = new DataView(memory.buffer).getBigInt64(6008, true);
assert.equal(directoryAfterRename > directoryAfterFileCreation, true);

env.dynlex_filesystem_clear_error();
assert.equal(env.dynlex_filesystem_create_directory(1160), -1);
const errorLength = env.dynlex_filesystem_error_message(0, 0);
assert.equal(errorLength > 0, true);
assert.equal(env.dynlex_filesystem_error_message(6100, 128), errorLength);
assert.match(new TextDecoder().decode(bytes.subarray(6100, 6100 + errorLength)), /exists/i);
assert.equal(env.dynlex_filesystem_remove(1160), -1, "non-empty directories must not be removed");
assert.equal(env.dynlex_filesystem_remove(1340), 0);
assert.equal(env.dynlex_filesystem_status(1160, 6000, 6008), 0);
assert.equal(new DataView(memory.buffer).getBigInt64(6008, true) > directoryAfterRename, true);
assert.equal(env.dynlex_filesystem_remove(1160), 0);

writeCString(1120, "persist.bin");
stream = env.fopen(1120, 1088);
assert.notEqual(stream, 0);
bytes.set([80, 81], 2052);
assert.equal(env.fwrite(2052, 1, 2, stream), 2);
assert.equal(env.fclose(stream), 0);

stream = env.tmpfile();
assert.notEqual(stream, 0);
bytes.set(encoder.encode("payload"), 4096);
assert.equal(env.fwrite(4096, 1, 7, stream), 7);
assert.equal(env.fflush(stream), 0);
env.rewind(stream);
assert.equal(env.fgetc(stream), 112);
assert.equal(env.fread(4128, 1, 6, stream), 6);
assert.equal(new TextDecoder().decode(bytes.subarray(4128, 4134)), "ayload");
assert.equal(env.fclose(stream), 0);

const allocation = env.calloc(3, 1);
bytes.set([7, 8, 9], allocation);
const resized = env.realloc(allocation, 5);
assert.notEqual(resized, 0);
assert.deepEqual([...bytes.subarray(resized, resized + 3)], [7, 8, 9]);
assert.equal(env.memchr(resized, 8, 3), resized + 1);
assert.equal(env.memchr(resized, 10, 3), 0);

bytes.set([10, 11, 12], 5000);
assert.equal(env.memcpy(5010, 5000, 3), 5010);
assert.deepEqual([...bytes.subarray(5010, 5013)], [10, 11, 12]);
const memorySizeBeforeInvalidCopy = memory.buffer.byteLength;
assert.throws(
  () => env.memcpy(memorySizeBeforeInvalidCopy + 16, 5000, 3),
  WebAssembly.RuntimeError
);
assert.throws(() => env.memcpy(5010, memorySizeBeforeInvalidCopy - 1, 3), WebAssembly.RuntimeError);
assert.throws(() => env.memmove(memorySizeBeforeInvalidCopy - 1, 5000, 3), WebAssembly.RuntimeError);
assert.throws(() => env.memmove(5010, memorySizeBeforeInvalidCopy - 1, 3), WebAssembly.RuntimeError);
assert.throws(() => env.memset(memorySizeBeforeInvalidCopy - 1, 0, 3), WebAssembly.RuntimeError);
assert.equal(memory.buffer.byteLength, memorySizeBeforeInvalidCopy, "memcpy must not grow program memory");

const secondRuntime = buildRuntimeImports(importSpecs, [], filesystem, emptyLayout).env;
const secondBytes = new Uint8Array(secondRuntime.__linear_memory.buffer);
secondBytes.set(encoder.encode("persist.bin\0"), 1024);
secondBytes.set(encoder.encode("rb\0"), 1056);
stream = secondRuntime.fopen(1024, 1056);
assert.notEqual(stream, 0, "files must persist across runs");
assert.equal(secondRuntime.fread(2048, 1, 2, stream), 2);
assert.deepEqual([...secondBytes.subarray(2048, 2050)], [80, 81]);
assert.equal(secondRuntime.fclose(stream), 0);

const allocatorRuntime = buildRuntimeImports([], [], createRuntimeFilesystem(), emptyLayout).env;
const releasedAllocation = allocatorRuntime.malloc(16);
assert.equal(releasedAllocation % 16, 0, "malloc must satisfy the wasm32 C maximum alignment");
allocatorRuntime.free(releasedAllocation);
assert.equal(allocatorRuntime.malloc(16), releasedAllocation, "freed runtime memory must be reusable");
const alignmentBlocker = allocatorRuntime.malloc(1);
const movedAllocation = allocatorRuntime.realloc(releasedAllocation, 33);
assert.equal(movedAllocation % 16, 0, "realloc must preserve the wasm32 C maximum alignment");
assert.equal(alignmentBlocker % 16, 0, "adjacent allocations must remain maximally aligned");
assert.equal(
  allocatorRuntime.calloc(3, 7) % 16,
  0,
  "calloc must satisfy the wasm32 C maximum alignment"
);

const boundaryRuntime = buildRuntimeImports([], [], createRuntimeFilesystem(), emptyLayout).env;
const boundaryMemory = boundaryRuntime.__linear_memory;
const boundaryHeapStart = boundaryRuntime.__stack_pointer.value;
assert.notEqual(boundaryRuntime.malloc(boundaryMemory.buffer.byteLength - boundaryHeapStart), 0);
const boundaryLength = boundaryMemory.buffer.byteLength;
assert.notEqual(boundaryRuntime.malloc(65536), 0);
assert.equal(
  boundaryMemory.buffer.byteLength,
  boundaryLength + 65536,
  "an allocation ending one page beyond memory must grow by exactly one page"
);

const timeImportRuntime = buildRuntimeImports(
  [{ module: "env", name: "time" }],
  [],
  createRuntimeFilesystem(),
  emptyLayout
).env;
const timeImportModule = new WebAssembly.Module(new Uint8Array([
  0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
  0x01, 0x06, 0x01, 0x60, 0x01, 0x7f, 0x01, 0x7e,
  0x02, 0x0c, 0x01, 0x03, 0x65, 0x6e, 0x76, 0x04, 0x74, 0x69, 0x6d, 0x65, 0x00, 0x00,
  0x07, 0x0d, 0x01, 0x09, 0x63, 0x61, 0x6c, 0x6c, 0x5f, 0x74, 0x69, 0x6d, 0x65, 0x00, 0x00
]));
const timeImportInstance = new WebAssembly.Instance(timeImportModule, { env: timeImportRuntime });
assert.equal(typeof timeImportInstance.exports.call_time(0), "bigint", "an i64 time import must return a BigInt");

const largeStaticDataEnd = 10 * 1024 * 1024 + 3;
const largeLayoutRuntime = buildRuntimeImports(
  [],
  [],
  createRuntimeFilesystem(),
  { staticDataEnd: largeStaticDataEnd }
).env;
const expectedStackEnd = Math.ceil(largeStaticDataEnd / 16) * 16 + 8 * 1024 * 1024;
assert.equal(largeLayoutRuntime.__stack_pointer.value, expectedStackEnd);
assert.equal(
  largeLayoutRuntime.malloc(1),
  expectedStackEnd,
  "runtime heap must start above both static data and the program stack"
);

console.log("Browser runtime filesystem imports passed");
