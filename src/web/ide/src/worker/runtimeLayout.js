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
