import assert from "node:assert/strict";

import {
  buildRuntimeImports,
  createRuntimeFilesystem,
  isSupportedRuntimeImport
} from "../../src/web/ide/public/compiler/runtimeImports.js";

const filesystemImportNames = [
  "abort",
  "dynlex_filesystem_clear_error",
  "dynlex_filesystem_create_directories",
  "dynlex_filesystem_directory_copy_name",
  "dynlex_filesystem_directory_next",
  "dynlex_filesystem_directory_open",
  "dynlex_filesystem_directory_release",
  "dynlex_filesystem_directory_retain",
  "dynlex_filesystem_entry",
  "dynlex_filesystem_error_message",
  "dynlex_filesystem_file_finish",
  "dynlex_filesystem_file_open",
  "dynlex_filesystem_file_read",
  "dynlex_filesystem_file_release",
  "dynlex_filesystem_file_retain",
  "dynlex_filesystem_file_rewind",
  "dynlex_filesystem_file_write",
  "dynlex_filesystem_remove_tree",
  "dynlex_filesystem_rename",
  "dynlex_filesystem_status",
  "dynlex_filesystem_staging_cancel",
  "dynlex_filesystem_staging_commit",
  "dynlex_filesystem_staging_copy_path",
  "dynlex_filesystem_staging_create",
  "dynlex_filesystem_staging_path_length",
  "dynlex_filesystem_staging_release",
  "dynlex_filesystem_staging_restore_metadata",
  "dynlex_filesystem_staging_retain",
  "dynlex_filesystem_staging_state",
  "dynlex_filesystem_staging_write",
  "dynlex_filesystem_temporary_directory_copy_path",
  "dynlex_filesystem_temporary_directory_create",
  "dynlex_filesystem_temporary_directory_path_length",
  "dynlex_filesystem_temporary_directory_release",
  "dynlex_filesystem_temporary_directory_retain",
  "dynlex_filesystem_temporary_file_open",
  "dynlex_filesystem_transactions_supported",
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
assert.equal(env.dynlex_filesystem_create_directories(1240, 13), 0, "parent directories are created recursively");
assert.equal(env.dynlex_filesystem_remove_tree(1240, 13), 0);
assert.equal(env.dynlex_filesystem_remove_tree(1240, 7), 0);
assert.equal(env.fopen(1270, 1088), 0, "a file requires an existing parent");
assert.equal(env.dynlex_filesystem_create_directories(1160, 18), 0);
assert.equal(env.dynlex_filesystem_create_directories(1160, 18), 0, "directory creation is idempotent");
assert.equal(env.dynlex_filesystem_status(1160, 18, 6000, 6008), 1);
let view = new DataView(memory.buffer);
assert.equal(view.getInt32(6000, true), 2);
const directoryCreationTime = view.getBigInt64(6008, true);
assert.equal(directoryCreationTime > 0n, true);
assert.equal(env.fopen(1160, 1096), 0, "directories must not open as regular files");

stream = env.fopen(1200, 1088);
assert.notEqual(stream, 0);
assert.equal(env.fwrite(2060, 1, 2, stream), 2);
assert.equal(env.fclose(stream), 0);
assert.equal(env.dynlex_filesystem_status(1160, 18, 6000, 6008), 1);
const directoryAfterFileCreation = new DataView(memory.buffer).getBigInt64(6008, true);
assert.equal(directoryAfterFileCreation > directoryCreationTime, true);
assert.equal(env.dynlex_filesystem_status(1200, 27, 6000, 6008), 1);
view = new DataView(memory.buffer);
assert.equal(view.getInt32(6000, true), 1);
assert.equal(view.getBigInt64(6008, true) > 0n, true);
assert.equal(
  env.dynlex_filesystem_entry(
    1200,
    27,
    6200,
    6204,
    6208,
    6216,
    6224,
    6232,
    6240,
    6248,
    6256,
    6260,
    6264,
    6272,
    6276,
    6280,
    6288,
    6292,
    6300,
    26,
    6330
  ),
  1
);
view = new DataView(memory.buffer);
assert.equal(view.getInt32(6200, true), 1);
assert.equal(view.getInt32(6204, true), 0, "web entries do not expose POSIX mode");
assert.equal(view.getInt32(6216, true), 0, "web entries do not expose Windows attributes");
assert.equal(view.getInt32(6240, true), 0, "web entries do not expose access time");
assert.equal(view.getInt32(6260, true), 1, "web entries expose modification time");
assert.equal(view.getBigInt64(6264, true) > 0n, true);
assert.equal(view.getInt32(6272, true) >= 0, true);
assert.equal(view.getInt32(6272, true) < 1000000000, true);
assert.equal(view.getInt32(6276, true), 0, "web entries do not expose creation time");
assert.equal(view.getInt32(6292, true), 0, "web entries do not expose native identity");
assert.equal(view.getUint32(6330, true), 0);
assert.equal(env.dynlex_filesystem_transactions_supported(), 0);
assert.equal(env.dynlex_filesystem_staging_create(1200, 27), 0);
assert.equal(env.dynlex_filesystem_staging_write(0, 0, 0), -2);
assert.equal(env.dynlex_filesystem_staging_restore_metadata(0), -2);
assert.equal(env.dynlex_filesystem_staging_cancel(0, 6340), -2);
assert.equal(view.getInt32(6340, true), 1);
assert.equal(env.dynlex_filesystem_staging_commit(0, 0, 0, 6344, 6348, 6352, 6356), -2);
assert.equal(view.getInt32(6344, true), 1);
assert.equal(view.getInt32(6348, true), 0);
assert.equal(view.getInt32(6352, true), 0);
assert.equal(view.getInt32(6356, true), 1);
assert.equal(env.rename(1200, 1270), -1, "rename requires an existing destination parent");
assert.equal(env.dynlex_filesystem_status(1200, 27, 6000, 6008), 1, "failed rename must preserve the source");
assert.equal(env.rename(1160, 1300), -1, "a directory cannot be renamed into its own subtree");
assert.equal(env.rename(1200, 1340), 0);
assert.equal(env.dynlex_filesystem_status(1160, 18, 6000, 6008), 1);
const directoryAfterRename = new DataView(memory.buffer).getBigInt64(6008, true);
assert.equal(directoryAfterRename > directoryAfterFileCreation, true);

env.dynlex_filesystem_clear_error();
assert.equal(env.dynlex_filesystem_create_directories(1160, 18), 0);
assert.equal(env.dynlex_filesystem_file_open(1160, 18, 1), 0);
const errorLength = env.dynlex_filesystem_error_message(0, 0);
assert.equal(errorLength > 0, true);
assert.equal(env.dynlex_filesystem_error_message(6100, 128), errorLength);
assert.match(new TextDecoder().decode(bytes.subarray(6100, 6100 + errorLength)), /regular file/i);
assert.equal(env.remove(1160), -1, "non-empty directories must not be removed by the C remove import");
assert.equal(env.remove(1340), 0);
assert.equal(env.dynlex_filesystem_status(1160, 18, 6000, 6008), 1);
assert.equal(new DataView(memory.buffer).getBigInt64(6008, true) > directoryAfterRename, true);
assert.equal(env.remove(1160), 0);

writeCString(1400, "capability-tree/one/two");
writeCString(1450, "capability-tree/payload.bin");
writeCString(1500, "capability-tree/link");
writeCString(1540, "outside.bin");
writeCString(1580, "missing-capability");
writeCString(1620, "capability-tree/device");
writeCString(1660, "capability-tree///");
assert.equal(env.dynlex_filesystem_create_directories(1400, 23), 0);
assert.equal(env.dynlex_filesystem_create_directories(1400, 23), 0);
assert.equal(
  env.dynlex_filesystem_status(1660, 18, 6000, 6008),
  1,
  "public runtime paths must ignore trailing separators consistently with native targets"
);

let runtimeFile = env.dynlex_filesystem_file_open(1450, 27, 2);
assert.notEqual(runtimeFile, 0);
env.dynlex_filesystem_file_retain(runtimeFile);
bytes.set([65, 0, 66, 67, 68], 7000);
assert.equal(env.dynlex_filesystem_file_write(runtimeFile, 7000, 5, 7020), 0);
assert.equal(new DataView(memory.buffer).getUint32(7020, true), 5);
assert.equal(env.dynlex_filesystem_file_finish(runtimeFile), 0);
env.dynlex_filesystem_file_release(runtimeFile);

filesystem.files.set("outside.bin", {
  data: new Uint8Array([99]),
  modificationTime: filesystem.lastTimestamp + 1
});
filesystem.symlinks.set("capability-tree/link", {
  target: "outside.bin",
  modificationTime: filesystem.lastTimestamp + 2
});
filesystem.others.set("capability-tree/device", {
  modificationTime: filesystem.lastTimestamp + 3
});
filesystem.lastTimestamp += 3;
assert.equal(env.dynlex_filesystem_status(1500, 20, 6000, 6008), 1);
assert.equal(new DataView(memory.buffer).getInt32(6000, true), 3, "status must classify links without following");
assert.equal(env.dynlex_filesystem_status(1620, 22, 6000, 6008), 1);
assert.equal(new DataView(memory.buffer).getInt32(6000, true), 4);
assert.equal(env.dynlex_filesystem_status(1580, 18, 6000, 6008), 0, "missing is not an error");

const directoryHandle = env.dynlex_filesystem_directory_open(1400, 15);
assert.notEqual(directoryHandle, 0);
env.dynlex_filesystem_directory_retain(directoryHandle);
const enumerated = new Map();
while (env.dynlex_filesystem_directory_next(directoryHandle, 7040, 7044) === 1) {
  const kind = new DataView(memory.buffer).getInt32(7040, true);
  const length = new DataView(memory.buffer).getUint32(7044, true);
  assert.equal(env.dynlex_filesystem_directory_copy_name(directoryHandle, 7060, length + 1), 0);
  enumerated.set(new TextDecoder().decode(bytes.subarray(7060, 7060 + length)), kind);
}
env.dynlex_filesystem_directory_release(directoryHandle);
assert.deepEqual(enumerated, new Map([
  ["payload.bin", 1],
  ["one", 2],
  ["link", 3],
  ["device", 4]
]));

runtimeFile = env.dynlex_filesystem_file_open(1450, 27, 1);
assert.notEqual(runtimeFile, 0);
env.dynlex_filesystem_file_retain(runtimeFile);
assert.equal(env.dynlex_filesystem_file_read(runtimeFile, 7100, 3, 7120, 7124), 0);
assert.equal(new DataView(memory.buffer).getUint32(7120, true), 3);
assert.equal(new DataView(memory.buffer).getInt32(7124, true), 0);
assert.deepEqual([...bytes.subarray(7100, 7103)], [65, 0, 66]);
assert.equal(env.dynlex_filesystem_file_read(runtimeFile, 7100, 3, 7120, 7124), 0);
assert.equal(new DataView(memory.buffer).getUint32(7120, true), 2);
assert.equal(new DataView(memory.buffer).getInt32(7124, true), 1);
assert.deepEqual([...bytes.subarray(7100, 7102)], [67, 68]);
env.dynlex_filesystem_file_release(runtimeFile);

const temporaryHandles = [
  env.dynlex_filesystem_temporary_directory_create(),
  env.dynlex_filesystem_temporary_directory_create()
];
const temporaryPaths = [];
for (const handle of temporaryHandles) {
  assert.notEqual(handle, 0);
  env.dynlex_filesystem_temporary_directory_retain(handle);
  const length = env.dynlex_filesystem_temporary_directory_path_length(handle);
  assert.equal(env.dynlex_filesystem_temporary_directory_copy_path(handle, 7200, length + 1), 0);
  temporaryPaths.push(new TextDecoder().decode(bytes.subarray(7200, 7200 + length)));
  env.dynlex_filesystem_temporary_directory_release(handle);
}
assert.equal(temporaryPaths.every((path) => path.startsWith("/tmp/")), true);
assert.notEqual(temporaryPaths[0], temporaryPaths[1]);
assert.equal(env.dynlex_filesystem_remove_tree(7200, new TextEncoder().encode(temporaryPaths[1]).length), 0);
writeCString(7200, temporaryPaths[0]);
assert.equal(env.dynlex_filesystem_remove_tree(7200, new TextEncoder().encode(temporaryPaths[0]).length), 0);

assert.equal(env.dynlex_filesystem_remove_tree(1400, 15), 0);
assert.equal(filesystem.files.has("outside.bin"), true, "tree removal must not traverse symbolic links");
assert.equal(env.dynlex_filesystem_status(1400, 15, 6000, 6008), 0);
bytes.set([0x80], 7300);
assert.equal(env.dynlex_filesystem_status(7300, 1, 6000, 6008), -1, "public paths require UTF-8");

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
