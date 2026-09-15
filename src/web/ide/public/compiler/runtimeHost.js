const encoder = new TextEncoder();

function integer(value) {
  const result = Number(value);
  return Number.isSafeInteger(result) && result >= 0 ? result : null;
}

function range(memory, pointerValue, lengthValue) {
  const start = integer(pointerValue);
  const length = integer(lengthValue);
  if (start === null || length === null) {
    return null;
  }
  const end = start + length;
  return Number.isSafeInteger(end) && end <= memory.buffer.byteLength ? { end, start } : null;
}

function writeMessage(memory, pointerValue, capacityValue, message) {
  const capacity = integer(capacityValue);
  const outputRange = range(memory, pointerValue, capacity ?? -1);
  if (capacity === null || !outputRange) {
    throw new WebAssembly.RuntimeError("Runtime error-message buffer is outside program memory");
  }
  if (outputRange.start && capacity > 0) {
    const encoded = encoder.encode(message);
    const contentLength = Math.min(encoded.length, capacity - 1);
    const bytes = new Uint8Array(memory.buffer);
    bytes.set(encoded.subarray(0, contentLength), outputRange.start);
    bytes[outputRange.start + contentLength] = 0;
  }
  return encoder.encode(message).length;
}

export function createHostImports(memory) {
  let lastError = "";

  function unsupportedText(outputLengthPointer, supportedPointer) {
    lastError = "This host operation is not available in the browser";
    const outputRange = range(memory, outputLengthPointer, 4);
    const supportedRange = range(memory, supportedPointer, 4);
    if (!outputRange || !supportedRange) {
      throw new WebAssembly.RuntimeError("Host result is outside program memory");
    }
    const view = new DataView(memory.buffer);
    view.setUint32(outputRange.start, 0, true);
    view.setInt32(supportedRange.start, 0, true);
    return 0;
  }

  return {
    dynlex_host_error_message(pointer, capacity) {
      return writeMessage(memory, pointer, capacity, lastError || "Host operation failed");
    },
    dynlex_host_executable_directory(output, capacity, outputLength, supported) {
      void output;
      void capacity;
      return unsupportedText(outputLength, supported);
    },
    dynlex_host_executable_path(output, capacity, outputLength, supported) {
      void output;
      void capacity;
      return unsupportedText(outputLength, supported);
    },
    dynlex_host_environment_value(name, nameLength, output, capacity, outputLength, found, supported) {
      void name; void nameLength; void output; void capacity;
      lastError = "Environment variables are not available in the browser";
      const lengthRange = range(memory, outputLength, 4);
      const foundRange = range(memory, found, 4);
      const supportedRange = range(memory, supported, 4);
      if (!lengthRange || !foundRange || !supportedRange) throw new WebAssembly.RuntimeError("Host result is outside program memory");
      const view = new DataView(memory.buffer);
      view.setUint32(lengthRange.start, 0, true);
      view.setInt32(foundRange.start, 0, true);
      view.setInt32(supportedRange.start, 0, true);
      return 0;
    },
    dynlex_host_find_executable(name, nameLength, output, capacity, outputLength, found, supported) {
      void name; void nameLength; void output; void capacity;
      lastError = "Executable discovery is not available in the browser";
      const lengthRange = range(memory, outputLength, 4);
      const foundRange = range(memory, found, 4);
      const supportedRange = range(memory, supported, 4);
      if (!lengthRange || !foundRange || !supportedRange) throw new WebAssembly.RuntimeError("Host result is outside program memory");
      const view = new DataView(memory.buffer);
      view.setUint32(lengthRange.start, 0, true);
      view.setInt32(foundRange.start, 0, true);
      view.setInt32(supportedRange.start, 0, true);
      return 0;
    },
    dynlex_host_platform_name(output, capacity, outputLength) {
      const bytes = new TextEncoder().encode("Browser");
      const lengthRange = range(memory, outputLength, 4);
      if (!lengthRange) throw new WebAssembly.RuntimeError("Host result is outside program memory");
      new DataView(memory.buffer).setUint32(lengthRange.start, bytes.length, true);
      if (output === 0) return 0;
      const outputRange = range(memory, output, capacity);
      if (!outputRange || capacity < bytes.length) return -1;
      new Uint8Array(memory.buffer, outputRange.start, bytes.length).set(bytes);
      return 0;
    },
    dynlex_host_is_administrator(administrator, supported) {
      const result = range(memory, administrator, 4);
      const supportedRange = range(memory, supported, 4);
      if (!result || !supportedRange) throw new WebAssembly.RuntimeError("Host result is outside program memory");
      new DataView(memory.buffer).setInt32(result.start, 0, true);
      new DataView(memory.buffer).setInt32(supportedRange.start, 0, true);
      return 0;
    },
    dynlex_host_write_standard_error(contents, length, supported) {
      void contents; void length;
      lastError = "Standard error is not available in the browser";
      const supportedRange = range(memory, supported, 4);
      if (!supportedRange) throw new WebAssembly.RuntimeError("Host result is outside program memory");
      new DataView(memory.buffer).setInt32(supportedRange.start, 0, true);
      return 0;
    },
    dynlex_host_exit(status) {
      throw new WebAssembly.RuntimeError(`Program exited with status ${status}`);
    },
    dynlex_host_user_cache_directory(output, capacity, outputLength, supported) {
      void output;
      void capacity;
      return unsupportedText(outputLength, supported);
    },
    dynlex_host_platform_is_windows(isWindows, supported) {
      lastError = "Host platform information is not available in the browser";
      const platformRange = range(memory, isWindows, 4);
      const supportedRange = range(memory, supported, 4);
      if (!platformRange || !supportedRange) {
        throw new WebAssembly.RuntimeError("Host platform result is outside program memory");
      }
      const view = new DataView(memory.buffer);
      view.setInt32(platformRange.start, 0, true);
      view.setInt32(supportedRange.start, 0, true);
      return 0;
    },
    dynlex_host_read_standard_input(contents, length, endOfFile, supported) {
      lastError = "Standard input is not available in the browser";
      const contentsRange = range(memory, contents, 4);
      const lengthRange = range(memory, length, 4);
      const endRange = range(memory, endOfFile, 4);
      const supportedRange = range(memory, supported, 4);
      if (!contentsRange || !lengthRange || !endRange || !supportedRange) {
        throw new WebAssembly.RuntimeError("Standard input result is outside program memory");
      }
      const view = new DataView(memory.buffer);
      view.setUint32(contentsRange.start, 0, true);
      view.setUint32(lengthRange.start, 0, true);
      view.setInt32(endRange.start, 0, true);
      view.setInt32(supportedRange.start, 0, true);
      return 0;
    }
  };
}
