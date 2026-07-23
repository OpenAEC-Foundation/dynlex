const POSIX = 1;
const WINDOWS = 2;
const encoder = new TextEncoder();
const fatalDecoder = new TextDecoder("utf-8", { fatal: true });

class PathError extends Error {}

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

function readBinary(memory, pointerValue, lengthValue) {
  const inputRange = range(memory, pointerValue, lengthValue);
  if (!inputRange || (inputRange.start === 0 && inputRange.end !== 0)) {
    throw new PathError("Invalid path text");
  }
  const bytes = new Uint8Array(memory.buffer).subarray(inputRange.start, inputRange.end);
  if (bytes.includes(0)) {
    throw new PathError("Path text contains a null byte");
  }
  const chunks = [];
  for (let offset = 0; offset < bytes.length; offset += 32768) {
    chunks.push(String.fromCharCode(...bytes.subarray(offset, offset + 32768)));
  }
  return chunks.join("");
}

function binaryBytes(text) {
  const result = new Uint8Array(text.length);
  for (let index = 0; index < text.length; index += 1) {
    result[index] = text.charCodeAt(index);
  }
  return result;
}

function validUtf8(text) {
  try {
    fatalDecoder.decode(binaryBytes(text));
    return true;
  } catch {
    return false;
  }
}

function asciiAlpha(value) {
  return (value >= "A" && value <= "Z") || (value >= "a" && value <= "z");
}

function asciiLower(text) {
  return text.replace(/[A-Z]/g, (value) => value.toLowerCase());
}

function separator(style, value) {
  return style === WINDOWS ? value === "/" || value === "\\" : value === "/";
}

function formatPath(rootKind, root, components) {
  let text = root;
  for (let index = 0; index < components.length; index += 1) {
    const driveFirst = rootKind === "windows-drive-relative" && index === 0;
    if (text && !text.endsWith("/") && !driveFirst) {
      text += "/";
    }
    text += components[index];
  }
  return text || ".";
}

function parseRoot(style, text) {
  if (style !== POSIX && style !== WINDOWS) {
    throw new PathError("Unknown path style");
  }
  if (style === POSIX) {
    let cursor = 0;
    while (cursor < text.length && text[cursor] === "/") {
      cursor += 1;
    }
    if (cursor === 0) {
      return { cursor, root: "", rootKind: "posix-relative" };
    }
    return {
      cursor,
      root: cursor === 2 ? "//" : "/",
      rootKind: cursor === 2 ? "posix-double" : "posix-single"
    };
  }

  if (
    text.length >= 4 &&
    separator(WINDOWS, text[0]) &&
    separator(WINDOWS, text[1]) &&
    (text[2] === "?" || text[2] === ".") &&
    separator(WINDOWS, text[3])
  ) {
    throw new PathError("Windows device namespace paths are not supported");
  }
  if (text.length >= 2 && asciiAlpha(text[0]) && text[1] === ":") {
    let cursor = 2;
    if (cursor < text.length && separator(WINDOWS, text[cursor])) {
      while (cursor < text.length && separator(WINDOWS, text[cursor])) {
        cursor += 1;
      }
      return { cursor, root: `${text.slice(0, 2)}/`, rootKind: "windows-drive-absolute" };
    }
    return { cursor, root: text.slice(0, 2), rootKind: "windows-drive-relative" };
  }
  if (text && separator(WINDOWS, text[0])) {
    const unc =
      text.length >= 2 &&
      separator(WINDOWS, text[1]) &&
      (text.length === 2 || !separator(WINDOWS, text[2]));
    if (!unc) {
      let cursor = 0;
      while (cursor < text.length && separator(WINDOWS, text[cursor])) {
        cursor += 1;
      }
      return { cursor, root: "/", rootKind: "windows-rooted" };
    }
    let serverEnd = 2;
    while (serverEnd < text.length && !separator(WINDOWS, text[serverEnd])) {
      serverEnd += 1;
    }
    let shareStart = serverEnd;
    while (shareStart < text.length && separator(WINDOWS, text[shareStart])) {
      shareStart += 1;
    }
    let shareEnd = shareStart;
    while (shareEnd < text.length && !separator(WINDOWS, text[shareEnd])) {
      shareEnd += 1;
    }
    const server = text.slice(2, serverEnd);
    const share = text.slice(shareStart, shareEnd);
    if (!server || !share || server === "." || server === "?" || share === "." || share === "..") {
      throw new PathError("A Windows UNC path requires a server and share");
    }
    let cursor = shareEnd;
    while (cursor < text.length && separator(WINDOWS, text[cursor])) {
      cursor += 1;
    }
    return { cursor, root: `//${server}/${share}`, rootKind: "windows-unc" };
  }
  return { cursor: 0, root: "", rootKind: "windows-relative" };
}

function rootIsDirectory(rootKind) {
  return [
    "posix-single",
    "posix-double",
    "windows-rooted",
    "windows-drive-absolute",
    "windows-unc"
  ].includes(rootKind);
}

function normalizePath(style, text) {
  const parsed = parseRoot(style, text);
  const components = [];
  let { cursor } = parsed;
  while (cursor < text.length) {
    while (cursor < text.length && separator(style, text[cursor])) {
      cursor += 1;
    }
    const start = cursor;
    while (cursor < text.length && !separator(style, text[cursor])) {
      cursor += 1;
    }
    const component = text.slice(start, cursor);
    if (!component || component === ".") {
      continue;
    }
    if (component === "..") {
      if (components.length && components.at(-1) !== "..") {
        components.pop();
      } else if (!rootIsDirectory(parsed.rootKind)) {
        components.push(component);
      }
    } else {
      components.push(component);
    }
  }
  return {
    components,
    root: parsed.root,
    rootKind: parsed.rootKind,
    text: formatPath(parsed.rootKind, parsed.root, components)
  };
}

function fullyAbsolute(style, path) {
  if (style === POSIX) {
    return path.rootKind === "posix-single" || path.rootKind === "posix-double";
  }
  return path.rootKind === "windows-drive-absolute" || path.rootKind === "windows-unc";
}

function absolutePath(style, path) {
  return fullyAbsolute(style, path) || (style === WINDOWS && path.rootKind === "windows-rooted");
}

function combine(style, left, addSeparator, right) {
  return normalizePath(style, `${left}${addSeparator ? "/" : ""}${right}`);
}

function joinPaths(style, left, right) {
  if (fullyAbsolute(style, right)) {
    return right;
  }
  if (style === WINDOWS && right.rootKind === "windows-rooted") {
    if (!left.root || left.rootKind === "windows-relative") {
      return right;
    }
    return combine(style, left.root, false, right.text);
  }
  if (style === WINDOWS && right.rootKind === "windows-drive-relative") {
    const leftHasSameDrive =
      ["windows-drive-relative", "windows-drive-absolute"].includes(left.rootKind) &&
      asciiLower(left.root.slice(0, 2)) === asciiLower(right.root.slice(0, 2));
    if (!leftHasSameDrive) {
      return right;
    }
    const relative = right.text.slice(2);
    return combine(style, left.text, Boolean(relative), relative);
  }
  const leftDot = !left.root && left.components.length === 0;
  const rightDot = !right.root && right.components.length === 0;
  if (leftDot) {
    return right;
  }
  if (rightDot) {
    return left;
  }
  return combine(style, left.text, true, right.text);
}

function resolvePath(style, path, base) {
  if (!fullyAbsolute(style, base)) {
    throw new PathError("Path resolution requires an absolute base path");
  }
  if (fullyAbsolute(style, path)) {
    return path;
  }
  if (style === WINDOWS && path.rootKind === "windows-rooted") {
    return combine(style, base.root, false, path.text);
  }
  if (style === WINDOWS && path.rootKind === "windows-drive-relative") {
    if (
      base.rootKind !== "windows-drive-absolute" ||
      asciiLower(path.root.slice(0, 2)) !== asciiLower(base.root.slice(0, 2))
    ) {
      throw new PathError("A drive-relative path requires an absolute base on the same drive");
    }
    const relative = path.text.slice(2);
    return normalizePath(
      style,
      `${path.root.slice(0, 2)}${base.text.slice(2)}${relative ? "/" : ""}${relative}`
    );
  }
  return joinPaths(style, base, path);
}

function rootsEqual(style, left, right) {
  if (left.rootKind !== right.rootKind || left.root.length !== right.root.length) {
    return false;
  }
  return style === WINDOWS
    ? asciiLower(left.root) === asciiLower(right.root)
    : left.root === right.root;
}

function relativePath(style, path, base) {
  if (!fullyAbsolute(style, path) || !fullyAbsolute(style, base)) {
    throw new PathError("Relative path calculation requires absolute target and base paths");
  }
  if (!rootsEqual(style, path, base)) {
    throw new PathError("Target and base paths have different roots");
  }
  let common = 0;
  while (common < path.components.length && common < base.components.length) {
    const left = path.components[common];
    const right = base.components[common];
    if ((style === WINDOWS ? asciiLower(left) === asciiLower(right) : left === right) === false) {
      break;
    }
    common += 1;
  }
  return [
    ...Array(base.components.length - common).fill(".."),
    ...path.components.slice(common)
  ].join("/") || ".";
}

function suffixSpan(filename) {
  if (filename === "." || filename === "..") {
    return filename.length;
  }
  const position = filename.lastIndexOf(".");
  return position > 0 ? position : filename.length;
}

function unaryPath(operation, path) {
  if (operation === 1) {
    return path.text;
  }
  if (operation === 2) {
    return path.components.length
      ? formatPath(path.rootKind, path.root, path.components.slice(0, -1))
      : path.text;
  }
  const filename = path.components.at(-1) ?? "";
  if (operation === 3) {
    return filename;
  }
  const suffix = suffixSpan(filename);
  if (operation === 4) {
    return filename.slice(0, suffix);
  }
  if (operation === 5) {
    return filename.slice(suffix);
  }
  throw new PathError("Unknown unary path operation");
}

function unreserved(byte) {
  return (
    (byte >= 65 && byte <= 90) ||
    (byte >= 97 && byte <= 122) ||
    (byte >= 48 && byte <= 57) ||
    [45, 46, 95, 126].includes(byte)
  );
}

function subdelimiter(byte) {
  return [33, 36, 38, 39, 40, 41, 42, 43, 44, 59, 61].includes(byte);
}

function percentEncode(text, authority) {
  const digits = "0123456789ABCDEF";
  let result = "";
  for (const byte of binaryBytes(text)) {
    const safe = authority
      ? unreserved(byte) || subdelimiter(byte)
      : unreserved(byte) || byte === 47 || byte === 58;
    result += safe ? String.fromCharCode(byte) : `%${digits[byte >> 4]}${digits[byte & 15]}`;
  }
  return result;
}

function encodeFileUri(style, path) {
  if (!fullyAbsolute(style, path)) {
    throw new PathError("A file URI requires an absolute path");
  }
  if (!validUtf8(path.text)) {
    throw new PathError("A file URI path must be valid UTF-8");
  }
  if (style === WINDOWS && path.rootKind === "windows-unc") {
    const serverEnd = path.root.indexOf("/", 2);
    return `file://${percentEncode(path.text.slice(2, serverEnd), true)}${percentEncode(
      path.text.slice(serverEnd),
      false
    )}`;
  }
  return `${style === WINDOWS ? "file:///" : "file://"}${percentEncode(path.text, false)}`;
}

function hex(value) {
  if (value >= "0" && value <= "9") {
    return value.charCodeAt(0) - 48;
  }
  const lower = value.toLowerCase();
  return lower >= "a" && lower <= "f" ? lower.charCodeAt(0) - 87 : -1;
}

function decodePercent(text, index, context) {
  if (index + 2 >= text.length) {
    throw new PathError(`A file URI ${context} contains an incomplete percent escape`);
  }
  const high = hex(text[index + 1]);
  const low = hex(text[index + 2]);
  if (high < 0 || low < 0) {
    throw new PathError(`A file URI ${context} contains an invalid percent escape`);
  }
  return { byte: (high << 4) | low, index: index + 2 };
}

function decodeAuthority(text) {
  let result = "";
  for (let index = 0; index < text.length; index += 1) {
    let byte = text.charCodeAt(index);
    if (byte === 37) {
      ({ byte, index } = decodePercent(text, index, "authority"));
      if (byte === 0 || byte === 47 || byte === 92) {
        throw new PathError("A file URI authority contains an invalid encoded byte");
      }
    } else if (!unreserved(byte) && !subdelimiter(byte)) {
      throw new PathError("A file URI contains an invalid authority");
    }
    result += String.fromCharCode(byte);
  }
  if (!validUtf8(result)) {
    throw new PathError("A file URI authority does not contain valid UTF-8");
  }
  return result;
}

function decodeUriPath(style, text) {
  let result = "";
  for (let index = 0; index < text.length; index += 1) {
    let byte = text.charCodeAt(index);
    if (byte === 63 || byte === 35) {
      throw new PathError("A file URI must not contain a query or fragment");
    }
    let encoded = false;
    if (byte === 37) {
      ({ byte, index } = decodePercent(text, index, "path"));
      encoded = true;
    } else if (!unreserved(byte) && !subdelimiter(byte) && ![47, 58, 64].includes(byte)) {
      throw new PathError("A file URI path contains an invalid character");
    }
    if (byte === 0) {
      throw new PathError("A file URI decodes to a null byte");
    }
    if (encoded && byte === 47) {
      throw new PathError("A file URI path must not encode a path separator");
    }
    if (style === WINDOWS && byte === 92) {
      throw new PathError("A Windows file URI path must use forward slashes");
    }
    result += String.fromCharCode(byte);
  }
  if (!validUtf8(result)) {
    throw new PathError("A file URI path does not contain valid UTF-8");
  }
  return result;
}

function decodeFileUri(style, uri) {
  if ([...uri].some((value) => value.charCodeAt(0) > 127)) {
    throw new PathError("A file URI must percent-encode non-ASCII bytes");
  }
  if (uri.length < 6 || asciiLower(uri.slice(0, 5)) !== "file:" || uri[5] !== "/") {
    throw new PathError("Expected an absolute file URI");
  }
  let authority = "";
  let pathStart = 5;
  if (uri.length >= 7 && uri[6] === "/") {
    const authorityStart = 7;
    pathStart = uri.indexOf("/", authorityStart);
    if (pathStart < 0) {
      throw new PathError("A file URI requires an absolute path");
    }
    authority = decodeAuthority(uri.slice(authorityStart, pathStart));
  }
  const local = !authority || asciiLower(authority) === "localhost";
  const decoded = decodeUriPath(style, uri.slice(pathStart));
  let pathText;
  if (!local) {
    pathText = `//${authority}${decoded}`;
  } else if (style === WINDOWS) {
    const uncPath = decoded.length >= 3 && decoded.startsWith("//") && decoded[2] !== "/";
    const invalidDrivePath =
      decoded.length < 4 ||
      decoded[0] !== "/" ||
      !asciiAlpha(decoded[1]) ||
      decoded[2] !== ":" ||
      decoded[3] !== "/";
    if (!uncPath && invalidDrivePath) {
      throw new PathError("A local Windows file URI requires an absolute drive or UNC path");
    }
    pathText = uncPath ? decoded : decoded.slice(1);
  } else {
    pathText = decoded;
  }
  const normalized = normalizePath(style, pathText);
  if (!fullyAbsolute(style, normalized)) {
    throw new PathError("A file URI did not contain an absolute path");
  }
  return normalized.text;
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

export function createPathImports(memory, allocateBytes) {
  let lastError = "";

  function fail(error) {
    lastError = error instanceof Error ? error.message : String(error);
    return -1;
  }

  function prepareOutput(outputPointer, lengthPointer) {
    const outputRange = range(memory, outputPointer, 4);
    const lengthRange = range(memory, lengthPointer, 4);
    if (!outputRange || !lengthRange) {
      throw new PathError("Invalid path output arguments");
    }
    const view = new DataView(memory.buffer);
    view.setUint32(outputRange.start, 0, true);
    view.setUint32(lengthRange.start, 0, true);
  }

  function writeOwned(text, outputPointer, lengthPointer) {
    prepareOutput(outputPointer, lengthPointer);
    let pointer = 0;
    if (text.length) {
      pointer = allocateBytes(text.length, false);
      if (!pointer) {
        throw new PathError("Could not allocate path result");
      }
      new Uint8Array(memory.buffer).set(binaryBytes(text), pointer);
    }
    const view = new DataView(memory.buffer);
    view.setUint32(Number(outputPointer), pointer, true);
    view.setUint32(Number(lengthPointer), text.length, true);
  }

  function withOwnedOutput(outputPointer, lengthPointer, operation) {
    lastError = "";
    try {
      prepareOutput(outputPointer, lengthPointer);
      writeOwned(operation(), outputPointer, lengthPointer);
      return 0;
    } catch (error) {
      return fail(error);
    }
  }

  return {
    dynlex_path_binary(operation, style, left, leftLength, right, rightLength, output, outputLength) {
      return withOwnedOutput(output, outputLength, () => {
        const leftPath = normalizePath(Number(style), readBinary(memory, left, leftLength));
        const rightPath = normalizePath(Number(style), readBinary(memory, right, rightLength));
        if (Number(operation) === 1) {
          return joinPaths(Number(style), leftPath, rightPath).text;
        }
        if (Number(operation) === 2) {
          return resolvePath(Number(style), leftPath, rightPath).text;
        }
        if (Number(operation) === 3) {
          return relativePath(Number(style), leftPath, rightPath);
        }
        throw new PathError("Unknown binary path operation");
      });
    },
    dynlex_path_error_message(pointer, capacity) {
      return writeMessage(memory, pointer, capacity, lastError || "Path operation failed");
    },
    dynlex_path_file_uri(operation, style, input, inputLength, output, outputLength) {
      return withOwnedOutput(output, outputLength, () => {
        const text = readBinary(memory, input, inputLength);
        if (Number(operation) === 1) {
          return encodeFileUri(Number(style), normalizePath(Number(style), text));
        }
        if (Number(operation) === 2) {
          return decodeFileUri(Number(style), text);
        }
        throw new PathError("Unknown file URI operation");
      });
    },
    dynlex_path_is_absolute(style, input, inputLength, resultPointer) {
      lastError = "";
      const resultRange = range(memory, resultPointer, 4);
      if (!resultRange) {
        return fail(new PathError("Invalid path query output argument"));
      }
      const view = new DataView(memory.buffer);
      view.setInt32(resultRange.start, 0, true);
      try {
        const path = normalizePath(Number(style), readBinary(memory, input, inputLength));
        view.setInt32(resultRange.start, absolutePath(Number(style), path) ? 1 : 0, true);
        return 0;
      } catch (error) {
        return fail(error);
      }
    },
    dynlex_path_native_style() {
      return POSIX;
    },
    dynlex_path_native_style_supported() {
      return 0;
    },
    dynlex_path_unary(operation, style, input, inputLength, output, outputLength) {
      return withOwnedOutput(output, outputLength, () =>
        unaryPath(
          Number(operation),
          normalizePath(Number(style), readBinary(memory, input, inputLength))
        )
      );
    }
  };
}

export function createHostImports(memory) {
  let lastError = "";

  function unsupported(outputLengthPointer) {
    lastError = "This host operation is not available in the browser";
    const outputRange = range(memory, outputLengthPointer, 4);
    if (!outputRange) {
      throw new WebAssembly.RuntimeError("Host output length is outside program memory");
    }
    new DataView(memory.buffer).setUint32(outputRange.start, 0, true);
    return -1;
  }

  return {
    dynlex_host_error_message(pointer, capacity) {
      return writeMessage(memory, pointer, capacity, lastError || "Host operation failed");
    },
    dynlex_host_executable_directory(output, capacity, outputLength) {
      void output;
      void capacity;
      return unsupported(outputLength);
    },
    dynlex_host_executable_path(output, capacity, outputLength) {
      void output;
      void capacity;
      return unsupported(outputLength);
    },
    dynlex_host_platform_is_windows() {
      return 0;
    },
    dynlex_host_read_standard_input_line(contents, length, endOfFile) {
      lastError = "Standard input is not available in the browser";
      const contentsRange = range(memory, contents, 4);
      const lengthRange = range(memory, length, 4);
      const endRange = range(memory, endOfFile, 4);
      if (!contentsRange || !lengthRange || !endRange) {
        throw new WebAssembly.RuntimeError("Standard input result is outside program memory");
      }
      const view = new DataView(memory.buffer);
      view.setUint32(contentsRange.start, 0, true);
      view.setUint32(lengthRange.start, 0, true);
      view.setInt32(endRange.start, 0, true);
      return -1;
    }
  };
}
