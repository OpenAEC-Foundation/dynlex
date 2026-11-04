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
    readName() {
      const length = this.readUnsignedLeb();
      const start = this.offset;
      this.skip(length);
      return new TextDecoder("utf-8", { fatal: true }).decode(this.bytes.subarray(start, this.offset));
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

// Linked DynLex modules define their globals locally and export LLD's heap
// boundary. This includes BSS and stack storage, which data segments omit.
export function inspectRuntimeWasmLayout(wasmBytes) {
  const reader = wasmReader(wasmBytes);
  const expectedHeader = [0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00];
  for (const expected of expectedHeader) {
    if (reader.readByte() !== expected) {
      throw new WebAssembly.CompileError("Invalid WebAssembly module header");
    }
  }

  const globals = [];
  let heapGlobalIndex = null;
  while (reader.offset < reader.bytes.length) {
    const sectionId = reader.readByte();
    const sectionSize = reader.readUnsignedLeb();
    const sectionEnd = reader.offset + sectionSize;
    if (!Number.isSafeInteger(sectionEnd) || sectionEnd > reader.bytes.length) {
      throw new WebAssembly.CompileError("WebAssembly section exceeds module length");
    }
    if (sectionId === 6) {
      const count = reader.readUnsignedLeb();
      for (let index = 0; index < count; index += 1) {
        const type = reader.readByte();
        const mutable = reader.readByte();
        if (type !== 0x7f || reader.readByte() !== 0x41) {
          throw new WebAssembly.CompileError("DynLex layout globals require constant i32 initializers");
        }
        const value = reader.readSignedI32Leb();
        if (reader.readByte() !== 0x0b) {
          throw new WebAssembly.CompileError("Invalid DynLex layout global initializer");
        }
        globals.push({ mutable, value });
      }
    } else if (sectionId === 7) {
      const count = reader.readUnsignedLeb();
      for (let index = 0; index < count; index += 1) {
        const name = reader.readName();
        const kind = reader.readByte();
        const globalIndex = reader.readUnsignedLeb();
        if (name === "__heap_base") {
          if (kind !== 3) {
            throw new WebAssembly.CompileError("DynLex heap boundary must be a global export");
          }
          heapGlobalIndex = globalIndex;
        }
      }
    } else {
      reader.offset = sectionEnd;
    }
    if (reader.offset !== sectionEnd) {
      throw new WebAssembly.CompileError("DynLex layout section length is inconsistent");
    }
  }
  const heap = heapGlobalIndex === null ? undefined : globals[heapGlobalIndex];
  if (!heap || heap.mutable !== 0 || heap.value <= 0 || heap.value % 16 !== 0) {
    throw new WebAssembly.CompileError("DynLex module has no valid linked heap boundary");
  }
  return { heapBase: heap.value };
}
