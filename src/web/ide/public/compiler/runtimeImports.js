import {
  createFileImports,
  createRuntimeFilesystem,
  readCString,
  writeCString
} from "./runtimeFilesystem.js";
import { createHostImports, createPathImports } from "./runtimePathHost.js";

const supportedEnvImports = new Set([
  "__indirect_function_table",
  "__linear_memory",
  "__memory_base",
  "__stack_pointer",
  "__table_base",
  "abort",
  "calloc",
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
  "dynlex_host_error_message",
  "dynlex_host_executable_directory",
  "dynlex_host_executable_path",
  "dynlex_host_environment_value",
  "dynlex_host_find_executable",
  "dynlex_host_exit",
  "dynlex_host_is_administrator",
  "dynlex_host_platform_name",
  "dynlex_host_platform_is_windows",
  "dynlex_host_read_standard_input",
  "dynlex_host_user_cache_directory",
  "dynlex_host_write_standard_error",
  "dynlex_path_binary",
  "dynlex_path_error_message",
  "dynlex_path_file_uri",
  "dynlex_path_is_absolute",
  "dynlex_path_native_style",
  "dynlex_path_unary",
  "dynlex_print_i64",
  "dynlex_print_string",
  "fclose",
  "ferror",
  "fflush",
  "fgetc",
  "fmax",
  "fmin",
  "fopen",
  "fprintf",
  "fread",
  "free",
  "fwrite",
  "malloc",
  "memchr",
  "memcpy",
  "memmove",
  "memset",
  "pow",
  "printf",
  "rand",
  "realloc",
  "remove",
  "rename",
  "rewind",
  "snprintf",
  "srand",
  "strlen",
  "time",
  "tmpfile"
]);

export { inspectRuntimeWasmLayout } from "./runtimeLayout.js";
export { createRuntimeFilesystem };

export function toNumber(value) {
  return Number(value);
}

export function toNonNegativeInteger(value) {
  const converted = toNumber(value);
  const number = Number.isFinite(converted) ? Math.trunc(converted) : 0;
  return number < 0 ? 0 : number;
}

export function isSupportedRuntimeImport(importSpec) {
  if (importSpec.module !== "env") {
    return false;
  }
  return supportedEnvImports.has(importSpec.name) || importSpec.name.startsWith("GOT.");
}

function cStringBytes(memory, pointer) {
  const bytes = new Uint8Array(memory.buffer);
  const start = toNonNegativeInteger(pointer);
  let end = start;
  while (end < bytes.length && bytes[end] !== 0) {
    end += 1;
  }
  return bytes.subarray(start, end);
}

function readUtf8(memory, pointer, length) {
  const bytes = new Uint8Array(memory.buffer);
  const start = toNonNegativeInteger(pointer);
  const maxLength = Math.max(0, Math.min(bytes.length - start, toNonNegativeInteger(length)));
  return new TextDecoder().decode(bytes.subarray(start, start + maxLength));
}

function createVarargsReader(memory, pointer) {
  let offset = toNonNegativeInteger(pointer);
  const view = new DataView(memory.buffer);

  function read(alignment, size, operation) {
    offset = Math.ceil(offset / alignment) * alignment;
    const end = offset + size;
    if (!Number.isSafeInteger(end) || end > memory.buffer.byteLength) {
      throw new WebAssembly.RuntimeError(`${operation} reads beyond program memory`);
    }
    const argumentOffset = offset;
    offset = end;
    return argumentOffset;
  }

  return {
    float64() {
      return view.getFloat64(read(8, 8, "variadic floating-point argument"), true);
    },
    int32() {
      return view.getInt32(read(4, 4, "variadic integer argument"), true);
    },
    int64() {
      return view.getBigInt64(read(8, 8, "variadic integer argument"), true);
    },
    pointer() {
      return view.getUint32(read(4, 4, "variadic pointer argument"), true);
    },
    uint32() {
      return view.getUint32(read(4, 4, "variadic integer argument"), true);
    },
    uint64() {
      return view.getBigUint64(read(8, 8, "variadic integer argument"), true);
    }
  };
}

function applyFormatWidth(text, width, flags, numeric = false) {
  if (width === null || text.length >= width) {
    return text;
  }
  const padding = (flags.includes("0") && !flags.includes("-") && numeric ? "0" : " ").repeat(width - text.length);
  if (flags.includes("-")) {
    return text + padding;
  }
  if (numeric && padding[0] === "0" && (text[0] === "-" || text[0] === "+" || text[0] === " ")) {
    return text[0] + padding + text.slice(1);
  }
  return padding + text;
}

function buildFormatStringOutput(memory, formatText, varargsPointer) {
  const args = createVarargsReader(memory, varargsPointer);
  let output = "";
  for (let i = 0; i < formatText.length; i += 1) {
    const char = formatText[i];
    if (char !== "%") {
      output += char;
      continue;
    }
    if (i + 1 >= formatText.length) {
      output += "%";
      break;
    }

    if (formatText[i + 1] === "%") {
      output += "%";
      i += 1;
      continue;
    }

    let cursor = i + 1;
    let flags = "";
    while (cursor < formatText.length && "-+ 0#".includes(formatText[cursor])) {
      flags += formatText[cursor++];
    }

    let width = null;
    if (formatText[cursor] === "*") {
      width = args.int32();
      cursor += 1;
      if (width < 0) {
        flags += "-";
        width = -width;
      }
    } else {
      const widthStart = cursor;
      while (cursor < formatText.length && /[0-9]/.test(formatText[cursor])) {
        cursor += 1;
      }
      if (cursor > widthStart) {
        width = Number(formatText.slice(widthStart, cursor));
      }
    }

    let precision = null;
    if (formatText[cursor] === ".") {
      cursor += 1;
      if (formatText[cursor] === "*") {
        precision = args.int32();
        cursor += 1;
        if (precision < 0) {
          precision = null;
        }
      } else {
        const precisionStart = cursor;
        while (cursor < formatText.length && /[0-9]/.test(formatText[cursor])) {
          cursor += 1;
        }
        precision = cursor === precisionStart ? 0 : Number(formatText.slice(precisionStart, cursor));
      }
    }

    let length = "";
    if (formatText.slice(cursor, cursor + 2) === "hh" || formatText.slice(cursor, cursor + 2) === "ll") {
      length = formatText.slice(cursor, cursor + 2);
      cursor += 2;
    } else if ("hljzt".includes(formatText[cursor] ?? "")) {
      length = formatText[cursor++];
    }

    const conversion = formatText[cursor];
    if (!conversion) {
      throw new WebAssembly.RuntimeError("Incomplete C format conversion");
    }
    i = cursor;
    let formatted;
    let numeric = false;
    if (conversion === "s") {
      if (length) {
        throw new WebAssembly.RuntimeError(`Unsupported C string format length '${length}'`);
      }
      const pointer = args.pointer();
      formatted = precision === null ? readCString(memory, pointer) : readUtf8(memory, pointer, precision);
    } else if (conversion === "c") {
      formatted = String.fromCharCode(args.int32() & 0xff);
    } else if (conversion === "d" || conversion === "i") {
      const value = length === "ll" || length === "j" ? args.int64() : args.int32();
      formatted = String(value);
      if (!formatted.startsWith("-") && (flags.includes("+") || flags.includes(" "))) {
        formatted = (flags.includes("+") ? "+" : " ") + formatted;
      }
      numeric = true;
    } else if ("uoxX".includes(conversion)) {
      const value = length === "ll" || length === "j" ? args.uint64() : args.uint32();
      const radix = conversion === "o" ? 8 : conversion === "u" ? 10 : 16;
      formatted = value.toString(radix);
      if (conversion === "X") {
        formatted = formatted.toUpperCase();
      }
      if (flags.includes("#") && value !== 0 && value !== 0n) {
        formatted = (conversion === "o" ? "0" : conversion === "X" ? "0X" : "0x") + formatted;
      }
      numeric = true;
    } else if ("fFeEgG".includes(conversion)) {
      if (length && length !== "l") {
        throw new WebAssembly.RuntimeError(`Unsupported C floating-point format length '${length}'`);
      }
      const value = args.float64();
      const digits = precision ?? 6;
      if (conversion === "f" || conversion === "F") {
        formatted = value.toFixed(digits);
      } else if (conversion === "e" || conversion === "E") {
        formatted = value.toExponential(digits);
      } else {
        formatted = value.toPrecision(digits === 0 ? 1 : digits);
      }
      if (conversion === conversion.toUpperCase()) {
        formatted = formatted.toUpperCase();
      }
      if (!formatted.startsWith("-") && (flags.includes("+") || flags.includes(" "))) {
        formatted = (flags.includes("+") ? "+" : " ") + formatted;
      }
      numeric = true;
    } else if (conversion === "p") {
      formatted = `0x${args.pointer().toString(16)}`;
      numeric = true;
    } else {
      throw new WebAssembly.RuntimeError(`Unsupported C format conversion '%${conversion}'`);
    }
    output += applyFormatWidth(formatted, width, flags, numeric);
  }
  return output;
}

export function buildRuntimeImports(importSpecs, stdoutChunks, filesystem, layout) {
  if (
    !filesystem ||
    !(filesystem.files instanceof Map) ||
    !(filesystem.directories instanceof Map) ||
    !(filesystem.others instanceof Map) ||
    !(filesystem.symlinks instanceof Map) ||
    !Number.isSafeInteger(filesystem.lastTimestamp) ||
    !Number.isSafeInteger(filesystem.temporaryDirectoryCounter)
  ) {
    throw new TypeError("DynLex runtime requires a filesystem");
  }
  if (!layout || !Number.isSafeInteger(layout.staticDataEnd) || layout.staticDataEnd < 0) {
    throw new TypeError("DynLex runtime requires a valid WebAssembly memory layout");
  }
  const pageSize = 65536;
  const maximumPages = 2048;
  const stackSize = 8 * 1024 * 1024;
  const allocationAlignment = 16;
  const stackBase = Math.ceil(layout.staticDataEnd / 16) * 16;
  const stackEnd = stackBase + stackSize;
  const initialPages = Math.max(256, Math.ceil(stackEnd / pageSize));
  if (!Number.isSafeInteger(stackEnd) || initialPages > maximumPages) {
    throw new WebAssembly.LinkError("DynLex program static data leaves no room for its runtime stack");
  }
  const memory = new WebAssembly.Memory({ initial: initialPages, maximum: maximumPages });
  const indirectFunctionTable = new WebAssembly.Table({ initial: 64, maximum: 2048, element: "anyfunc" });
  const stackPointer = new WebAssembly.Global({ value: "i32", mutable: true }, stackEnd);
  const memoryBase = new WebAssembly.Global({ value: "i32", mutable: false }, 0);
  const tableBase = new WebAssembly.Global({ value: "i32", mutable: false }, 0);
  const allocationSizes = new Map();
  const freeBlocks = [];
  let heapPointer = stackEnd;
  let randomState = 0x12345678;

  function byteCountValue(value) {
    const count = toNumber(value);
    return Number.isSafeInteger(count) && count >= 0 ? count : null;
  }

  function allocationSizeFor(byteCount) {
    return Math.max(
      allocationAlignment,
      Math.ceil(byteCount / allocationAlignment) * allocationAlignment
    );
  }

  function ensureMemoryForAddress(address) {
    const requiredAddress = toNonNegativeInteger(address);
    if (requiredAddress <= memory.buffer.byteLength) {
      return true;
    }
    const requiredPages = Math.ceil((requiredAddress - memory.buffer.byteLength) / 65536);
    try {
      memory.grow(requiredPages);
      return true;
    } catch {
      return false;
    }
  }

  function insertFreeBlock(pointer, size) {
    let index = 0;
    while (index < freeBlocks.length && freeBlocks[index].pointer < pointer) {
      index += 1;
    }
    freeBlocks.splice(index, 0, { pointer, size });

    if (index > 0) {
      const previous = freeBlocks[index - 1];
      const current = freeBlocks[index];
      if (previous.pointer + previous.size === current.pointer) {
        previous.size += current.size;
        freeBlocks.splice(index, 1);
        index -= 1;
      }
    }
    if (index + 1 < freeBlocks.length) {
      const current = freeBlocks[index];
      const next = freeBlocks[index + 1];
      if (current.pointer + current.size === next.pointer) {
        current.size += next.size;
        freeBlocks.splice(index + 1, 1);
      }
    }
  }

  function releaseAllocation(pointer) {
    const address = toNonNegativeInteger(pointer);
    if (address === 0) {
      return;
    }
    const allocation = allocationSizes.get(address);
    if (!allocation) {
      throw new WebAssembly.RuntimeError("free received an unknown allocation");
    }
    allocationSizes.delete(address);
    insertFreeBlock(address, allocation.size);
  }

  function allocateBytes(byteCount, zeroFill) {
    const count = byteCountValue(byteCount);
    if (count === null) {
      return 0;
    }
    const allocationSize = allocationSizeFor(count);
    let pointer = 0;
    const freeIndex = freeBlocks.findIndex((block) => block.size >= allocationSize);
    if (freeIndex >= 0) {
      const block = freeBlocks[freeIndex];
      pointer = block.pointer;
      block.pointer += allocationSize;
      block.size -= allocationSize;
      if (block.size === 0) {
        freeBlocks.splice(freeIndex, 1);
      }
    } else {
      pointer = heapPointer;
      const endPointer = pointer + allocationSize;
      if (!Number.isSafeInteger(endPointer) || !ensureMemoryForAddress(endPointer)) {
        return 0;
      }
      heapPointer = endPointer;
    }
    if (zeroFill && count > 0) {
      new Uint8Array(memory.buffer).fill(0, pointer, pointer + count);
    }
    allocationSizes.set(pointer, { requested: count, size: allocationSize });
    return pointer;
  }

  const env = {
    __indirect_function_table: indirectFunctionTable,
    __linear_memory: memory,
    __memory_base: memoryBase,
    __stack_pointer: stackPointer,
    __table_base: tableBase,
    abort() {
      throw new WebAssembly.RuntimeError("Program aborted");
    },
    calloc(count, size) {
      const itemCount = byteCountValue(count);
      const itemSize = byteCountValue(size);
      if (itemCount === null || itemSize === null || !Number.isSafeInteger(itemCount * itemSize)) {
        return 0;
      }
      return allocateBytes(itemCount * itemSize, true);
    },
    dynlex_print_i64(value) {
      stdoutChunks.push(String(value));
      return 0;
    },
    dynlex_print_string(pointer, length) {
      stdoutChunks.push(readUtf8(memory, pointer, length));
      return 0;
    },
    fmax(left, right) {
      return Math.max(toNumber(left), toNumber(right));
    },
    fmin(left, right) {
      return Math.min(toNumber(left), toNumber(right));
    },
    fprintf(stream, formatPointer, varargsPointer) {
      const output = buildFormatStringOutput(memory, readCString(memory, formatPointer), varargsPointer);
      const encoded = new TextEncoder().encode(output);
      const temporary = allocateBytes(encoded.length, false);
      if (!temporary) {
        return -1;
      }
      new Uint8Array(memory.buffer).set(encoded, temporary);
      const written = env.fwrite(temporary, 1, encoded.length, stream);
      releaseAllocation(temporary);
      return written;
    },
    free(pointer) {
      releaseAllocation(pointer);
    },
    malloc(size) {
      return allocateBytes(size, false);
    },
    memchr(pointer, value, length) {
      const start = toNonNegativeInteger(pointer);
      const count = toNonNegativeInteger(length);
      const end = start + count;
      if (!Number.isSafeInteger(end) || end > memory.buffer.byteLength) {
        throw new WebAssembly.RuntimeError("memchr range exceeds program memory");
      }
      const bytes = new Uint8Array(memory.buffer);
      const expected = toNonNegativeInteger(value) & 0xff;
      for (let index = start; index < end; index += 1) {
        if (bytes[index] === expected) {
          return index;
        }
      }
      return 0;
    },
    memcpy(destination, source, length) {
      const destinationStart = toNonNegativeInteger(destination);
      const sourceStart = toNonNegativeInteger(source);
      const count = toNonNegativeInteger(length);
      const destinationEnd = destinationStart + count;
      const sourceEnd = sourceStart + count;
      if (
        !Number.isSafeInteger(destinationEnd) ||
        !Number.isSafeInteger(sourceEnd) ||
        destinationEnd > memory.buffer.byteLength ||
        sourceEnd > memory.buffer.byteLength
      ) {
        throw new WebAssembly.RuntimeError("memcpy range exceeds program memory");
      }
      const bytes = new Uint8Array(memory.buffer);
      bytes.set(bytes.subarray(sourceStart, sourceEnd), destinationStart);
      return destinationStart;
    },
    memmove(destination, source, length) {
      const destinationStart = toNonNegativeInteger(destination);
      const sourceStart = toNonNegativeInteger(source);
      const count = toNonNegativeInteger(length);
      const destinationEnd = destinationStart + count;
      const sourceEnd = sourceStart + count;
      if (
        !Number.isSafeInteger(destinationEnd) ||
        !Number.isSafeInteger(sourceEnd) ||
        destinationEnd > memory.buffer.byteLength ||
        sourceEnd > memory.buffer.byteLength
      ) {
        throw new WebAssembly.RuntimeError("memmove range exceeds program memory");
      }
      const bytes = new Uint8Array(memory.buffer);
      bytes.copyWithin(destinationStart, sourceStart, sourceEnd);
      return destinationStart;
    },
    memset(destination, value, length) {
      const destinationStart = toNonNegativeInteger(destination);
      const count = toNonNegativeInteger(length);
      const destinationEnd = destinationStart + count;
      if (!Number.isSafeInteger(destinationEnd) || destinationEnd > memory.buffer.byteLength) {
        throw new WebAssembly.RuntimeError("memset range exceeds program memory");
      }
      new Uint8Array(memory.buffer).fill(
        toNonNegativeInteger(value) & 0xff,
        destinationStart,
        destinationEnd
      );
      return destinationStart;
    },
    pow(left, right) {
      return Math.pow(toNumber(left), toNumber(right));
    },
    printf(formatPointer, varargsPointer) {
      const output = buildFormatStringOutput(memory, readCString(memory, formatPointer), varargsPointer);
      stdoutChunks.push(output);
      return new TextEncoder().encode(output).length;
    },
    rand() {
      randomState = (1103515245 * randomState + 12345) & 0x7fffffff;
      return randomState;
    },
    realloc(pointer, size) {
      const source = toNonNegativeInteger(pointer);
      const requested = byteCountValue(size);
      if (requested === null) {
        return 0;
      }
      if (source === 0) {
        return allocateBytes(requested, false);
      }
      const sourceAllocation = allocationSizes.get(source);
      if (!sourceAllocation) {
        throw new WebAssembly.RuntimeError("realloc received an unknown allocation");
      }
      if (requested === 0) {
        releaseAllocation(source);
        return 0;
      }
      const resizedAllocationSize = allocationSizeFor(requested);
      if (resizedAllocationSize <= sourceAllocation.size) {
        if (resizedAllocationSize < sourceAllocation.size) {
          insertFreeBlock(source + resizedAllocationSize, sourceAllocation.size - resizedAllocationSize);
          sourceAllocation.size = resizedAllocationSize;
        }
        sourceAllocation.requested = requested;
        return source;
      }
      const replacement = allocateBytes(requested, false);
      if (!replacement) {
        return 0;
      }
      const copied = Math.min(sourceAllocation.requested, requested);
      new Uint8Array(memory.buffer).copyWithin(replacement, source, source + copied);
      releaseAllocation(source);
      return replacement;
    },
    snprintf(bufferPointer, bufferSize, formatPointer, varargsPointer) {
      const output = buildFormatStringOutput(memory, readCString(memory, formatPointer), varargsPointer);
      writeCString(memory, bufferPointer, output, toNonNegativeInteger(bufferSize));
      return new TextEncoder().encode(output).length;
    },
    srand(seed) {
      randomState = toNonNegativeInteger(seed) || 1;
    },
    strlen(pointer) {
      const bytes = new Uint8Array(memory.buffer);
      let end = toNonNegativeInteger(pointer);
      const start = end;
      while (end < bytes.length && bytes[end] !== 0) {
        end += 1;
      }
      return end - start;
    },
    time() {
      return BigInt(Math.floor(Date.now() / 1000));
    },
    ...createFileImports(memory, filesystem),
    ...createHostImports(memory),
    ...createPathImports(memory, allocateBytes)
  };

  for (const importSpec of importSpecs) {
    if (importSpec.module === "env" && importSpec.name.startsWith("GOT.") && !(importSpec.name in env)) {
      env[importSpec.name] = new WebAssembly.Global({ value: "i32", mutable: false }, 0);
    }
  }

  return { env };
}
