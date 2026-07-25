export function createRuntimeFilesystem() {
  const timestamp = Date.now();
  return {
    directories: new Map([
      ["", timestamp],
      ["/", timestamp],
      ["/tmp", timestamp]
    ]),
    files: new Map(),
    lastTimestamp: timestamp,
    others: new Map(),
    symlinks: new Map(),
    temporaryDirectoryCounter: 0
  };
}

function toNonNegativeInteger(value) {
  const converted = Number(value);
  const number = Number.isFinite(converted) ? Math.trunc(converted) : 0;
  return number < 0 ? 0 : number;
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

export function readCString(memory, pointer) {
  return new TextDecoder().decode(cStringBytes(memory, pointer));
}

function readRawPath(memory, pointer) {
  const bytes = cStringBytes(memory, pointer);
  const chunks = [];
  for (let offset = 0; offset < bytes.length; offset += 32768) {
    chunks.push(String.fromCharCode(...bytes.subarray(offset, offset + 32768)));
  }
  return chunks.join("");
}

function readPublicPath(memory, pointer, lengthValue) {
  const start = toNonNegativeInteger(pointer);
  const length = toNonNegativeInteger(lengthValue);
  const end = start + length;
  if (length === 0 || !Number.isSafeInteger(end) || end > memory.buffer.byteLength) {
    return null;
  }
  const data = new Uint8Array(memory.buffer).subarray(start, end);
  if (data.includes(0)) {
    return null;
  }
  try {
    return new TextDecoder("utf-8", { fatal: true }).decode(data);
  } catch {
    return null;
  }
}

export function writeCString(memory, pointer, text, limit) {
  const bytes = new Uint8Array(memory.buffer);
  const start = toNonNegativeInteger(pointer);
  const encoded = new TextEncoder().encode(text);
  const maxContent = limit > 0 ? Math.max(0, Math.min(limit - 1, encoded.length)) : 0;
  bytes.set(encoded.subarray(0, maxContent), start);
  if (limit > 0 && start + maxContent < bytes.length) {
    bytes[start + maxContent] = 0;
  }
}

export function createFileImports(memory, filesystem) {
  const streams = new Map();
  const directories = new Map();
  const temporaryDirectories = new Map();
  let nextStream = 1;
  let nextDirectory = 1;
  let nextTemporaryDirectory = 1;
  let lastErrorMessage = "";

  function clearError() {
    lastErrorMessage = "";
  }

  function fail(message, result = -1) {
    lastErrorMessage = message;
    return result;
  }

  function nextModificationTime() {
    filesystem.lastTimestamp = Math.max(Date.now(), filesystem.lastTimestamp + 1);
    return filesystem.lastTimestamp;
  }

  function allEntryPaths() {
    return [
      ...filesystem.files.keys(),
      ...filesystem.directories.keys(),
      ...filesystem.others.keys(),
      ...filesystem.symlinks.keys()
    ];
  }

  function pathHasChildren(path) {
    const prefix = path.endsWith("/") ? path : `${path}/`;
    return allEntryPaths().some((candidate) => candidate !== path && candidate.startsWith(prefix));
  }

  function parentDirectory(path) {
    const separator = path.lastIndexOf("/");
    if (separator < 0) {
      return "";
    }
    return separator === 0 ? "/" : path.slice(0, separator);
  }

  function requireParentDirectory(path) {
    if (!filesystem.directories.has(parentDirectory(path))) {
      fail("Parent directory does not exist");
      return false;
    }
    return true;
  }

  function touchParentDirectories(...paths) {
    const parents = new Set(paths.map(parentDirectory));
    for (const parent of parents) {
      if (!filesystem.directories.has(parent)) {
        throw new WebAssembly.RuntimeError("Filesystem directory hierarchy is inconsistent");
      }
      filesystem.directories.set(parent, nextModificationTime());
    }
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

  function streamFor(handle) {
    return streams.get(toNonNegativeInteger(handle));
  }

  function openNode(node, mode) {
    const handle = nextStream++;
    streams.set(handle, {
      append: mode.startsWith("a"),
      endOfFile: false,
      error: false,
      node,
      position: mode.startsWith("a") ? node.data.length : 0,
      readable: mode.startsWith("r") || mode.includes("+"),
      references: 0,
      writable: mode.startsWith("w") || mode.startsWith("a") || mode.includes("+")
    });
    return handle;
  }

  function openPath(path, mode) {
    if (!path || !/^[rwa][b+]*$/.test(mode)) {
      return fail("Invalid filesystem path or file mode", 0);
    }
    if (filesystem.directories.has(path) || filesystem.others.has(path) || filesystem.symlinks.has(path)) {
      return fail("Path is not a regular file", 0);
    }
    let node = filesystem.files.get(path);
    if (mode.startsWith("r")) {
      return node ? openNode(node, mode) : fail("No such file or directory", 0);
    }
    if (mode.startsWith("w")) {
      const created = !node;
      if (created && !requireParentDirectory(path)) {
        return 0;
      }
      node = { data: new Uint8Array(0), modificationTime: nextModificationTime() };
      filesystem.files.set(path, node);
      if (created) {
        touchParentDirectories(path);
      }
      return openNode(node, mode);
    }
    if (!node) {
      if (!requireParentDirectory(path)) {
        return 0;
      }
      node = { data: new Uint8Array(0), modificationTime: nextModificationTime() };
      filesystem.files.set(path, node);
      touchParentDirectories(path);
    }
    return openNode(node, mode);
  }

  function resizeNode(node, size) {
    if (node.data.length >= size) {
      return;
    }
    const resized = new Uint8Array(size);
    resized.set(node.data);
    node.data = resized;
  }

  function readStream(buffer, countValue, handle, readCountPointer = null, eofPointer = null) {
    const stream = streamFor(handle);
    const requested = toNonNegativeInteger(countValue);
    if (!stream || !stream.readable) {
      if (stream) {
        stream.error = true;
      }
      return fail("File stream is not readable", readCountPointer === null ? 0 : -1);
    }
    const available = Math.max(0, stream.node.data.length - stream.position);
    const transferred = Math.min(requested, available);
    const range = byteRange(buffer, transferred);
    if (!range) {
      stream.error = true;
      return fail("File read buffer is outside program memory", readCountPointer === null ? 0 : -1);
    }
    new Uint8Array(memory.buffer).set(
      stream.node.data.subarray(stream.position, stream.position + transferred),
      range.start
    );
    stream.position += transferred;
    stream.endOfFile = transferred < requested;
    if (readCountPointer !== null) {
      const countRange = byteRange(readCountPointer, 4);
      const eofRange = byteRange(eofPointer, 4);
      if (!countRange || !eofRange) {
        stream.error = true;
        return fail("File read output is outside program memory");
      }
      const view = new DataView(memory.buffer);
      view.setUint32(countRange.start, transferred, true);
      view.setInt32(eofRange.start, stream.endOfFile ? 1 : 0, true);
      return 0;
    }
    return transferred;
  }

  function writeStream(buffer, countValue, handle, writtenPointer = null) {
    const stream = streamFor(handle);
    const requested = toNonNegativeInteger(countValue);
    const range = byteRange(buffer, requested);
    if (!stream || !stream.writable || !range) {
      if (stream) {
        stream.error = true;
      }
      return fail("File write failed", writtenPointer === null ? 0 : -1);
    }
    if (stream.append) {
      stream.position = stream.node.data.length;
    }
    resizeNode(stream.node, stream.position + requested);
    stream.node.data.set(new Uint8Array(memory.buffer).subarray(range.start, range.end), stream.position);
    stream.position += requested;
    stream.node.modificationTime = nextModificationTime();
    if (writtenPointer !== null) {
      const outputRange = byteRange(writtenPointer, 4);
      if (!outputRange) {
        return fail("File write output is outside program memory");
      }
      new DataView(memory.buffer).setUint32(outputRange.start, requested, true);
      return 0;
    }
    return requested;
  }

  function removeOne(path) {
    if (filesystem.files.delete(path) || filesystem.others.delete(path) || filesystem.symlinks.delete(path)) {
      touchParentDirectories(path);
      return 0;
    }
    if (!filesystem.directories.has(path)) {
      return fail("No such file or directory");
    }
    if (pathHasChildren(path)) {
      return fail("Directory is not empty");
    }
    if (path === "" || path === "/" || path === "/tmp") {
      return fail("Runtime filesystem root cannot be removed");
    }
    filesystem.directories.delete(path);
    touchParentDirectories(path);
    return 0;
  }

  function removeTree(path) {
    if (
      !filesystem.files.has(path) &&
      !filesystem.directories.has(path) &&
      !filesystem.others.has(path) &&
      !filesystem.symlinks.has(path)
    ) {
      return 0;
    }
    if (path === "" || path === "/" || path === "/tmp") {
      return fail("Runtime filesystem root cannot be removed");
    }
    if (filesystem.symlinks.delete(path) || filesystem.others.delete(path) || filesystem.files.delete(path)) {
      touchParentDirectories(path);
      return 0;
    }
    const prefix = path.endsWith("/") ? path : `${path}/`;
    for (const candidate of [...filesystem.files.keys()]) {
      if (candidate.startsWith(prefix)) {
        filesystem.files.delete(candidate);
      }
    }
    for (const candidate of [...filesystem.symlinks.keys()]) {
      if (candidate.startsWith(prefix)) {
        filesystem.symlinks.delete(candidate);
      }
    }
    for (const candidate of [...filesystem.others.keys()]) {
      if (candidate.startsWith(prefix)) {
        filesystem.others.delete(candidate);
      }
    }
    const childDirectories = [...filesystem.directories.keys()].filter((candidate) =>
      candidate.startsWith(prefix)
    );
    for (const candidate of childDirectories) {
      filesystem.directories.delete(candidate);
    }
    filesystem.directories.delete(path);
    touchParentDirectories(path);
    return 0;
  }

  function renamePath(source, destination) {
    const node = filesystem.files.get(source);
    if (!source || !destination) {
      return fail("Invalid filesystem path");
    }
    if (source === destination) {
      return node || filesystem.directories.has(source) || filesystem.others.has(source) || filesystem.symlinks.has(source)
        ? 0
        : fail("No such file or directory");
    }
    if (!requireParentDirectory(destination)) {
      return -1;
    }
    if (node) {
      if (
        filesystem.directories.has(destination) ||
        filesystem.others.has(destination) ||
        filesystem.symlinks.has(destination)
      ) {
        return fail("Destination path is not a regular file");
      }
      filesystem.files.delete(source);
      filesystem.files.set(destination, node);
      touchParentDirectories(source, destination);
      return 0;
    }
    const specialMap = filesystem.symlinks.has(source)
      ? filesystem.symlinks
      : filesystem.others.has(source)
        ? filesystem.others
        : null;
    if (specialMap) {
      if (
        filesystem.files.has(destination) ||
        filesystem.directories.has(destination) ||
        filesystem.others.has(destination) ||
        filesystem.symlinks.has(destination)
      ) {
        return fail("Destination path already exists");
      }
      const special = specialMap.get(source);
      specialMap.delete(source);
      specialMap.set(destination, special);
      touchParentDirectories(source, destination);
      return 0;
    }
    if (!filesystem.directories.has(source)) {
      return fail("No such file or directory");
    }
    if (destination.startsWith(`${source}/`)) {
      return fail("A directory cannot be moved into itself");
    }
    if (
      filesystem.files.has(destination) ||
      filesystem.directories.has(destination) ||
      filesystem.others.has(destination) ||
      filesystem.symlinks.has(destination)
    ) {
      return fail("Destination path already exists");
    }
    const sourcePrefix = `${source}/`;
    const movedFiles = [...filesystem.files.entries()].filter(([path]) => path.startsWith(sourcePrefix));
    const movedDirectories = [...filesystem.directories.entries()].filter(([path]) =>
      path.startsWith(sourcePrefix)
    );
    const movedLinks = [...filesystem.symlinks.entries()].filter(([path]) => path.startsWith(sourcePrefix));
    const movedOthers = [...filesystem.others.entries()].filter(([path]) => path.startsWith(sourcePrefix));
    const sourceModificationTime = filesystem.directories.get(source);
    filesystem.directories.delete(source);
    filesystem.directories.set(destination, sourceModificationTime);
    for (const [path, childNode] of movedFiles) {
      filesystem.files.delete(path);
      filesystem.files.set(destination + path.slice(source.length), childNode);
    }
    for (const [path, modificationTime] of movedDirectories) {
      filesystem.directories.delete(path);
      filesystem.directories.set(destination + path.slice(source.length), modificationTime);
    }
    for (const [path, childLink] of movedLinks) {
      filesystem.symlinks.delete(path);
      filesystem.symlinks.set(destination + path.slice(source.length), childLink);
    }
    for (const [path, childOther] of movedOthers) {
      filesystem.others.delete(path);
      filesystem.others.set(destination + path.slice(source.length), childOther);
    }
    touchParentDirectories(source, destination);
    return 0;
  }

  function statusForPath(path, kindPointer, modificationTimePointer) {
    const node = filesystem.files.get(path);
    const directoryTime = filesystem.directories.get(path);
    const link = filesystem.symlinks.get(path);
    const other = filesystem.others.get(path);
    if (!node && directoryTime === undefined && !link && !other) {
      return 0;
    }
    const kindRange = byteRange(kindPointer, 4);
    const modificationRange = byteRange(modificationTimePointer, 8);
    if (!kindRange || !modificationRange) {
      return fail("Filesystem status output is outside program memory");
    }
    const view = new DataView(memory.buffer);
    view.setInt32(kindRange.start, node ? 1 : directoryTime !== undefined ? 2 : link ? 3 : 4, true);
    view.setBigInt64(
      modificationRange.start,
      BigInt(node?.modificationTime ?? directoryTime ?? link?.modificationTime ?? other.modificationTime),
      true
    );
    return 1;
  }

  function publicPath(pointer, length) {
    let path = readPublicPath(memory, pointer, length);
    if (path === null) {
      fail("Filesystem paths must be nonempty UTF-8 text without zero bytes");
      return null;
    }
    while (path.length > 1 && path.endsWith("/")) {
      path = path.slice(0, -1);
    }
    return path;
  }

  return {
    fclose(handle) {
      const key = toNonNegativeInteger(handle);
      if (!streams.has(key)) {
        return fail("Invalid file stream");
      }
      streams.delete(key);
      return 0;
    },
    ferror(handle) {
      return streamFor(handle)?.error ? 1 : 0;
    },
    fflush(handle) {
      return streamFor(handle) ? 0 : fail("Invalid file stream");
    },
    fgetc(handle) {
      const stream = streamFor(handle);
      if (!stream || !stream.readable) {
        if (stream) {
          stream.error = true;
        }
        fail("File stream is not readable");
        return -1;
      }
      return stream.position >= stream.node.data.length ? -1 : stream.node.data[stream.position++];
    },
    fopen(pathPointer, modePointer) {
      return openPath(readRawPath(memory, pathPointer), readCString(memory, modePointer));
    },
    fread(buffer, sizeValue, countValue, handle) {
      const size = toNonNegativeInteger(sizeValue);
      const count = toNonNegativeInteger(countValue);
      const requested = size * count;
      if (!Number.isSafeInteger(requested) || size === 0 || count === 0) {
        return 0;
      }
      return Math.floor(readStream(buffer, requested, handle) / size);
    },
    fwrite(buffer, sizeValue, countValue, handle) {
      const size = toNonNegativeInteger(sizeValue);
      const count = toNonNegativeInteger(countValue);
      const requested = size * count;
      if (!Number.isSafeInteger(requested) || size === 0 || count === 0) {
        return 0;
      }
      return Math.floor(writeStream(buffer, requested, handle) / size);
    },
    remove(pathPointer) {
      return removeOne(readRawPath(memory, pathPointer));
    },
    rename(sourcePointer, destinationPointer) {
      return renamePath(readRawPath(memory, sourcePointer), readRawPath(memory, destinationPointer));
    },
    rewind(handle) {
      const stream = streamFor(handle);
      if (!stream) {
        throw new WebAssembly.RuntimeError("rewind received an invalid stream");
      }
      stream.position = 0;
      stream.endOfFile = false;
      stream.error = false;
    },
    tmpfile() {
      return openNode({ data: new Uint8Array(0), modificationTime: nextModificationTime() }, "w+b");
    },
    dynlex_filesystem_clear_error() {
      clearError();
    },
    dynlex_filesystem_error_message(bufferPointer, capacityValue) {
      if (!lastErrorMessage) {
        lastErrorMessage = "Filesystem operation failed";
      }
      const capacity = toNonNegativeInteger(capacityValue);
      if (bufferPointer && capacity > 0) {
        const range = byteRange(bufferPointer, capacity);
        if (!range) {
          throw new WebAssembly.RuntimeError("Filesystem error-message buffer is outside program memory");
        }
        writeCString(memory, bufferPointer, lastErrorMessage, capacity);
      }
      return new TextEncoder().encode(lastErrorMessage).length;
    },
    dynlex_filesystem_status(pathPointer, pathLength, kindPointer, modificationTimePointer) {
      clearError();
      const path = publicPath(pathPointer, pathLength);
      return path === null ? -1 : statusForPath(path, kindPointer, modificationTimePointer);
    },
    dynlex_filesystem_create_directories(pathPointer, pathLength) {
      clearError();
      const path = publicPath(pathPointer, pathLength);
      if (path === null) {
        return -1;
      }
      const rootLength = path.startsWith("/") ? 1 : 0;
      for (let index = rootLength; index <= path.length; index += 1) {
        if (index !== path.length && path[index] !== "/") {
          continue;
        }
        const component = path.slice(0, index);
        if (!component || component === "/") {
          continue;
        }
        if (filesystem.directories.has(component)) {
          continue;
        }
        if (
          filesystem.files.has(component) ||
          filesystem.others.has(component) ||
          filesystem.symlinks.has(component)
        ) {
          return fail("Filesystem path component is not a directory");
        }
        if (!requireParentDirectory(component)) {
          return -1;
        }
        filesystem.directories.set(component, nextModificationTime());
        touchParentDirectories(component);
      }
      return 0;
    },
    dynlex_filesystem_remove_tree(pathPointer, pathLength) {
      clearError();
      const path = publicPath(pathPointer, pathLength);
      return path === null ? -1 : removeTree(path);
    },
    dynlex_filesystem_rename(sourcePointer, sourceLength, destinationPointer, destinationLength) {
      clearError();
      const source = publicPath(sourcePointer, sourceLength);
      const destination = publicPath(destinationPointer, destinationLength);
      return source === null || destination === null ? -1 : renamePath(source, destination);
    },
    dynlex_filesystem_directory_open(pathPointer, pathLength) {
      clearError();
      const path = publicPath(pathPointer, pathLength);
      if (path === null) {
        return 0;
      }
      if (!filesystem.directories.has(path)) {
        return fail("Filesystem path is not a directory", 0);
      }
      const entries = [];
      for (const [candidate, kind] of [
        ...[...filesystem.files.keys()].map((entry) => [entry, 1]),
        ...[...filesystem.directories.keys()].map((entry) => [entry, 2]),
        ...[...filesystem.symlinks.keys()].map((entry) => [entry, 3]),
        ...[...filesystem.others.keys()].map((entry) => [entry, 4])
      ]) {
        if (candidate !== path && parentDirectory(candidate) === path) {
          entries.push({ kind, name: candidate.slice(path === "/" ? 1 : path.length + 1) });
        }
      }
      const handle = nextDirectory++;
      directories.set(handle, { current: null, entries, index: 0, references: 0 });
      return handle;
    },
    dynlex_filesystem_directory_retain(handle) {
      const directory = directories.get(toNonNegativeInteger(handle));
      if (!directory) {
        throw new WebAssembly.RuntimeError("Directory retain received an invalid handle");
      }
      directory.references += 1;
    },
    dynlex_filesystem_directory_release(handle) {
      const key = toNonNegativeInteger(handle);
      const directory = directories.get(key);
      if (!directory || directory.references === 0) {
        throw new WebAssembly.RuntimeError("Directory release received an invalid handle");
      }
      directory.references -= 1;
      if (directory.references === 0) {
        directories.delete(key);
      }
    },
    dynlex_filesystem_directory_next(handle, kindPointer, lengthPointer) {
      clearError();
      const directory = directories.get(toNonNegativeInteger(handle));
      const kindRange = byteRange(kindPointer, 4);
      const lengthRange = byteRange(lengthPointer, 4);
      if (!directory || !kindRange || !lengthRange) {
        return fail("Invalid directory enumeration arguments");
      }
      if (directory.index >= directory.entries.length) {
        return 0;
      }
      directory.current = directory.entries[directory.index++];
      const view = new DataView(memory.buffer);
      view.setInt32(kindRange.start, directory.current.kind, true);
      view.setUint32(lengthRange.start, new TextEncoder().encode(directory.current.name).length, true);
      return 1;
    },
    dynlex_filesystem_directory_copy_name(handle, bufferPointer, capacityValue) {
      clearError();
      const directory = directories.get(toNonNegativeInteger(handle));
      const capacity = toNonNegativeInteger(capacityValue);
      if (!directory?.current || !byteRange(bufferPointer, capacity)) {
        return fail("Directory entry name buffer is too small");
      }
      writeCString(memory, bufferPointer, directory.current.name, capacity);
      return 0;
    },
    dynlex_filesystem_file_open(pathPointer, pathLength, modeValue) {
      clearError();
      const path = publicPath(pathPointer, pathLength);
      const mode = Number(modeValue) === 1 ? "rb" : Number(modeValue) === 2 ? "wb" : Number(modeValue) === 3 ? "ab" : "";
      return path === null ? 0 : openPath(path, mode);
    },
    dynlex_filesystem_temporary_file_open() {
      clearError();
      return openNode({ data: new Uint8Array(0), modificationTime: nextModificationTime() }, "w+b");
    },
    dynlex_filesystem_file_retain(handle) {
      const stream = streamFor(handle);
      if (!stream) {
        throw new WebAssembly.RuntimeError("File retain received an invalid handle");
      }
      stream.references += 1;
    },
    dynlex_filesystem_file_release(handle) {
      const key = toNonNegativeInteger(handle);
      const stream = streams.get(key);
      if (!stream || stream.references === 0) {
        throw new WebAssembly.RuntimeError("File release received an invalid handle");
      }
      stream.references -= 1;
      if (stream.references === 0) {
        streams.delete(key);
      }
    },
    dynlex_filesystem_file_read(handle, buffer, capacity, readCountPointer, eofPointer) {
      clearError();
      return readStream(buffer, capacity, handle, readCountPointer, eofPointer);
    },
    dynlex_filesystem_file_write(handle, buffer, count, writtenPointer) {
      clearError();
      return writeStream(buffer, count, handle, writtenPointer);
    },
    dynlex_filesystem_file_finish(handle) {
      clearError();
      return streamFor(handle) ? 0 : fail("Invalid filesystem stream");
    },
    dynlex_filesystem_file_rewind(handle) {
      clearError();
      const stream = streamFor(handle);
      if (!stream) {
        return fail("Invalid filesystem stream");
      }
      stream.position = 0;
      stream.endOfFile = false;
      stream.error = false;
      return 0;
    },
    dynlex_filesystem_temporary_directory_create() {
      clearError();
      let path;
      do {
        filesystem.temporaryDirectoryCounter += 1;
        path = `/tmp/dynlex-${Date.now().toString(16)}-${filesystem.temporaryDirectoryCounter.toString(16)}`;
      } while (
        filesystem.files.has(path) ||
        filesystem.directories.has(path) ||
        filesystem.others.has(path) ||
        filesystem.symlinks.has(path)
      );
      filesystem.directories.set(path, nextModificationTime());
      touchParentDirectories(path);
      const handle = nextTemporaryDirectory++;
      temporaryDirectories.set(handle, { path, references: 0 });
      return handle;
    },
    dynlex_filesystem_temporary_directory_path_length(handle) {
      const result = temporaryDirectories.get(toNonNegativeInteger(handle));
      return result ? new TextEncoder().encode(result.path).length : 0;
    },
    dynlex_filesystem_temporary_directory_copy_path(handle, bufferPointer, capacityValue) {
      clearError();
      const result = temporaryDirectories.get(toNonNegativeInteger(handle));
      const capacity = toNonNegativeInteger(capacityValue);
      if (!result || !byteRange(bufferPointer, capacity)) {
        return fail("Temporary directory path buffer is too small");
      }
      writeCString(memory, bufferPointer, result.path, capacity);
      return 0;
    },
    dynlex_filesystem_temporary_directory_retain(handle) {
      const result = temporaryDirectories.get(toNonNegativeInteger(handle));
      if (!result) {
        throw new WebAssembly.RuntimeError("Temporary directory retain received an invalid handle");
      }
      result.references += 1;
    },
    dynlex_filesystem_temporary_directory_release(handle) {
      const key = toNonNegativeInteger(handle);
      const result = temporaryDirectories.get(key);
      if (!result || result.references === 0) {
        throw new WebAssembly.RuntimeError("Temporary directory release received an invalid handle");
      }
      result.references -= 1;
      if (result.references === 0) {
        temporaryDirectories.delete(key);
      }
    }
  };
}
