const supportedEnvImports = new Set([
  "__indirect_function_table",
  "__linear_memory",
  "__memory_base",
  "__stack_pointer",
  "__table_base",
  "abort",
  "calloc",
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

export function createRuntimeFilesystem() {
  return { files: new Map() };
}

function wasmReader(wasmBytes) {
  const bytes = wasmBytes instanceof Uint8Array ? wasmBytes : new Uint8Array(wasmBytes);
  return {
    bytes,
    offset: 0,
    readByte() {
      if (this.offset >= this.bytes.length) {
        throw new WebAssembly.CompileError("Unexpected end of WebAssembly module");
      }
      return this.bytes[this.offset++];
    },
    readUnsignedLeb() {
      let result = 0n;
      let shift = 0n;
      for (let index = 0; index < 5; index += 1) {
        const byte = this.readByte();
        result |= BigInt(byte & 0x7f) << shift;
        if ((byte & 0x80) === 0) {
          const number = Number(result);
          if (!Number.isSafeInteger(number)) {
            throw new WebAssembly.CompileError("WebAssembly integer exceeds JavaScript's safe range");
          }
          return number;
        }
        shift += 7n;
      }
      throw new WebAssembly.CompileError("Invalid WebAssembly unsigned LEB128 integer");
    },
    readSignedI32Leb() {
      let result = 0n;
      let shift = 0n;
      let byte = 0;
      for (let index = 0; index < 5; index += 1) {
        byte = this.readByte();
        result |= BigInt(byte & 0x7f) << shift;
        shift += 7n;
        if ((byte & 0x80) === 0) {
          if ((byte & 0x40) !== 0 && shift < 32n) {
            result |= -1n << shift;
          }
          return Number(BigInt.asIntN(32, result));
        }
      }
      throw new WebAssembly.CompileError("Invalid WebAssembly signed LEB128 integer");
    },
    skip(length) {
      const end = this.offset + length;
      if (!Number.isSafeInteger(end) || end > this.bytes.length) {
        throw new WebAssembly.CompileError("WebAssembly section exceeds module length");
      }
      this.offset = end;
    }
  };
}

function readActiveDataOffset(reader) {
  if (reader.readByte() !== 0x41) {
    throw new WebAssembly.CompileError("DynLex data segment has a non-constant offset");
  }
  const offset = reader.readSignedI32Leb();
  if (offset < 0 || reader.readByte() !== 0x0b) {
    throw new WebAssembly.CompileError("DynLex data segment has an invalid offset expression");
  }
  return offset;
}

export function inspectRuntimeWasmLayout(wasmBytes) {
  const reader = wasmReader(wasmBytes);
  const expectedHeader = [0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00];
  for (const expected of expectedHeader) {
    if (reader.readByte() !== expected) {
      throw new WebAssembly.CompileError("Invalid WebAssembly module header");
    }
  }

  let staticDataEnd = 0;
  while (reader.offset < reader.bytes.length) {
    const sectionId = reader.readByte();
    const sectionSize = reader.readUnsignedLeb();
    const sectionEnd = reader.offset + sectionSize;
    if (!Number.isSafeInteger(sectionEnd) || sectionEnd > reader.bytes.length) {
      throw new WebAssembly.CompileError("WebAssembly section exceeds module length");
    }
    if (sectionId !== 11) {
      reader.offset = sectionEnd;
      continue;
    }

    const segmentCount = reader.readUnsignedLeb();
    for (let index = 0; index < segmentCount; index += 1) {
      const flags = reader.readUnsignedLeb();
      let offset = null;
      if (flags === 0) {
        offset = readActiveDataOffset(reader);
      } else if (flags === 2) {
        const memoryIndex = reader.readUnsignedLeb();
        if (memoryIndex !== 0) {
          throw new WebAssembly.CompileError("DynLex data segment targets an unsupported memory");
        }
        offset = readActiveDataOffset(reader);
      } else if (flags !== 1) {
        throw new WebAssembly.CompileError(`Unsupported WebAssembly data segment flags ${flags}`);
      }
      const dataLength = reader.readUnsignedLeb();
      if (offset !== null) {
        const segmentEnd = offset + dataLength;
        if (!Number.isSafeInteger(segmentEnd)) {
          throw new WebAssembly.CompileError("DynLex static data size exceeds JavaScript's safe range");
        }
        staticDataEnd = Math.max(staticDataEnd, segmentEnd);
      }
      reader.skip(dataLength);
    }
    if (reader.offset !== sectionEnd) {
      throw new WebAssembly.CompileError("DynLex data section length is inconsistent");
    }
  }
  return { staticDataEnd };
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

function readCString(memory, pointer) {
  return new TextDecoder().decode(cStringBytes(memory, pointer));
}

function readFilesystemPath(memory, pointer) {
  const bytes = cStringBytes(memory, pointer);
  const chunks = [];
  for (let offset = 0; offset < bytes.length; offset += 32768) {
    chunks.push(String.fromCharCode(...bytes.subarray(offset, offset + 32768)));
  }
  return chunks.join("");
}

function readUtf8(memory, pointer, length) {
  const bytes = new Uint8Array(memory.buffer);
  const start = toNonNegativeInteger(pointer);
  const maxLength = Math.max(0, Math.min(bytes.length - start, toNonNegativeInteger(length)));
  return new TextDecoder().decode(bytes.subarray(start, start + maxLength));
}

function writeCString(memory, pointer, text, limit) {
  const bytes = new Uint8Array(memory.buffer);
  const start = toNonNegativeInteger(pointer);
  const encoded = new TextEncoder().encode(text);
  const maxContent = limit > 0 ? Math.max(0, Math.min(limit - 1, encoded.length)) : 0;
  bytes.set(encoded.subarray(0, maxContent), start);
  if (limit > 0 && start + maxContent < bytes.length) {
    bytes[start + maxContent] = 0;
  }
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

function createFileImports(memory, filesystem) {
  const streams = new Map();
  let nextStream = 1;

  function streamFor(handle) {
    return streams.get(toNonNegativeInteger(handle));
  }

  function openNode(node, mode) {
    const handle = nextStream++;
    streams.set(handle, {
      append: mode.startsWith("a"),
      error: false,
      node,
      position: mode.startsWith("a") ? node.data.length : 0,
      readable: mode.startsWith("r") || mode.includes("+"),
      writable: mode.startsWith("w") || mode.startsWith("a") || mode.includes("+")
    });
    return handle;
  }

  function byteRange(pointer, length) {
    const start = toNonNegativeInteger(pointer);
    const count = toNonNegativeInteger(length);
    const end = start + count;
    if (!Number.isSafeInteger(end) || end > memory.buffer.byteLength) {
      return null;
    }
    return { count, end, start };
  }

  function resizeNode(node, size) {
    if (node.data.length >= size) {
      return;
    }
    const resized = new Uint8Array(size);
    resized.set(node.data);
    node.data = resized;
  }

  return {
    fclose(handle) {
      const key = toNonNegativeInteger(handle);
      if (!streams.has(key)) {
        return -1;
      }
      streams.delete(key);
      return 0;
    },
    ferror(handle) {
      const stream = streamFor(handle);
      return stream?.error ? 1 : 0;
    },
    fflush(handle) {
      return streamFor(handle) ? 0 : -1;
    },
    fgetc(handle) {
      const stream = streamFor(handle);
      if (!stream || !stream.readable) {
        if (stream) {
          stream.error = true;
        }
        return -1;
      }
      if (stream.position >= stream.node.data.length) {
        return -1;
      }
      return stream.node.data[stream.position++];
    },
    fopen(pathPointer, modePointer) {
      const path = readFilesystemPath(memory, pathPointer);
      const mode = readCString(memory, modePointer);
      if (!path || !/^[rwa][b+]*$/.test(mode)) {
        return 0;
      }
      let node = filesystem.files.get(path);
      if (mode.startsWith("r")) {
        return node ? openNode(node, mode) : 0;
      }
      if (mode.startsWith("w")) {
        node = { data: new Uint8Array(0) };
        filesystem.files.set(path, node);
        return openNode(node, mode);
      }
      if (!node) {
        node = { data: new Uint8Array(0) };
        filesystem.files.set(path, node);
      }
      return openNode(node, mode);
    },
    fread(buffer, sizeValue, countValue, handle) {
      const stream = streamFor(handle);
      const size = toNonNegativeInteger(sizeValue);
      const count = toNonNegativeInteger(countValue);
      const requested = size * count;
      if (!stream || !stream.readable || !Number.isSafeInteger(requested)) {
        if (stream) {
          stream.error = true;
        }
        return 0;
      }
      if (size === 0 || count === 0) {
        return 0;
      }
      const available = Math.max(0, stream.node.data.length - stream.position);
      const transferred = Math.min(requested, available);
      const range = byteRange(buffer, transferred);
      if (!range) {
        stream.error = true;
        return 0;
      }
      new Uint8Array(memory.buffer).set(
        stream.node.data.subarray(stream.position, stream.position + transferred),
        range.start
      );
      stream.position += transferred;
      return Math.floor(transferred / size);
    },
    fwrite(buffer, sizeValue, countValue, handle) {
      const stream = streamFor(handle);
      const size = toNonNegativeInteger(sizeValue);
      const count = toNonNegativeInteger(countValue);
      const requested = size * count;
      const range = byteRange(buffer, requested);
      if (!stream || !stream.writable || !Number.isSafeInteger(requested) || !range) {
        if (stream) {
          stream.error = true;
        }
        return 0;
      }
      if (size === 0 || count === 0) {
        return 0;
      }
      if (stream.append) {
        stream.position = stream.node.data.length;
      }
      resizeNode(stream.node, stream.position + requested);
      stream.node.data.set(new Uint8Array(memory.buffer).subarray(range.start, range.end), stream.position);
      stream.position += requested;
      return count;
    },
    remove(pathPointer) {
      const path = readFilesystemPath(memory, pathPointer);
      return path && filesystem.files.delete(path) ? 0 : -1;
    },
    rename(sourcePointer, destinationPointer) {
      const source = readFilesystemPath(memory, sourcePointer);
      const destination = readFilesystemPath(memory, destinationPointer);
      const node = filesystem.files.get(source);
      if (!source || !destination || !node) {
        return -1;
      }
      filesystem.files.delete(source);
      filesystem.files.set(destination, node);
      return 0;
    },
    rewind(handle) {
      const stream = streamFor(handle);
      if (!stream) {
        throw new WebAssembly.RuntimeError("rewind received an invalid stream");
      }
      stream.position = 0;
      stream.error = false;
    },
    tmpfile() {
      return openNode({ data: new Uint8Array(0) }, "w+b");
    }
  };
}

export function buildRuntimeImports(importSpecs, stdoutChunks, filesystem, layout) {
  if (!filesystem || !(filesystem.files instanceof Map)) {
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
    ...createFileImports(memory, filesystem)
  };

  for (const importSpec of importSpecs) {
    if (importSpec.module === "env" && importSpec.name.startsWith("GOT.") && !(importSpec.name in env)) {
      env[importSpec.name] = new WebAssembly.Global({ value: "i32", mutable: false }, 0);
    }
  }

  return { env };
}
