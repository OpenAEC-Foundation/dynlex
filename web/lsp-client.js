export class LspResponseError extends Error {
  constructor(error) {
    super(typeof error?.message === "string" ? error.message : "Language server request failed");
    this.name = "LspResponseError";
    this.code = Number.isInteger(error?.code) ? error.code : -32603;
    this.data = error?.data;
  }
}

async function initializeLsp(client, { capabilities = {}, initializationOptions } = {}) {
  if (
    !client
    || typeof client.request !== "function"
    || typeof client.notify !== "function"
    || !capabilities
    || typeof capabilities !== "object"
    || (
      initializationOptions !== undefined
      && (!initializationOptions || typeof initializationOptions !== "object")
    )
  ) {
    throw new TypeError("LSP initialization requires a client and valid options");
  }
  const result = await client.request("initialize", {
    processId: null,
    rootUri: "file:///workspace",
    capabilities,
    ...(initializationOptions === undefined ? {} : { initializationOptions })
  });
  await client.notify("initialized", {});
  return result;
}

async function shutdownLsp(client) {
  if (!client || typeof client.request !== "function" || typeof client.notify !== "function") {
    throw new TypeError("LSP shutdown requires a client");
  }
  await client.request("shutdown", {});
  await client.notify("exit", {});
}

export class LspClient {
  constructor(exchange) {
    if (typeof exchange !== "function") {
      throw new TypeError("LSP exchange function is required");
    }
    this.exchange = exchange;
    this.nextRequestId = 1;
    this.notificationHandlers = new Map();
    this.requestHandlers = new Map();
  }

  onNotification(method, handler) {
    return this.#registerHandler(this.notificationHandlers, method, handler);
  }

  onRequest(method, handler) {
    return this.#registerHandler(this.requestHandlers, method, handler);
  }

  async request(method, params = {}) {
    const id = this.nextRequestId++;
    const messages = await this.#exchange({
      jsonrpc: "2.0",
      id,
      method,
      params
    });

    let response;
    for (const message of messages) {
      if (message.id === id && !Object.hasOwn(message, "method")) {
        if (response !== undefined) {
          throw new Error(`Language server returned multiple responses for ${method}`);
        }
        response = message;
      } else {
        await this.#handleServerMessage(message);
      }
    }

    if (response === undefined) {
      throw new Error(`Language server request ${method} did not return a response`);
    }
    if (response.error) {
      throw new LspResponseError(response.error);
    }
    if (!Object.hasOwn(response, "result")) {
      throw new Error(`Language server response for ${method} has no result`);
    }
    return response.result;
  }

  async notify(method, params = {}) {
    const messages = await this.#exchange({
      jsonrpc: "2.0",
      method,
      params
    });
    for (const message of messages) {
      await this.#handleServerMessage(message);
    }
  }

  #registerHandler(registry, method, handler) {
    if (typeof method !== "string" || method.length === 0 || typeof handler !== "function") {
      throw new TypeError("LSP handler requires a method and function");
    }
    if (registry.has(method)) {
      throw new Error(`LSP handler already registered for ${method}`);
    }
    registry.set(method, handler);
    return {
      dispose: () => {
        if (registry.get(method) === handler) {
          registry.delete(method);
        }
      }
    };
  }

  async #exchange(message) {
    const messages = await this.exchange(message);
    if (!Array.isArray(messages)) {
      throw new Error("Language server exchange must return an array of JSON-RPC messages");
    }
    for (const incoming of messages) {
      if (!incoming || typeof incoming !== "object" || incoming.jsonrpc !== "2.0") {
        throw new Error("Language server returned an invalid JSON-RPC message");
      }
    }
    return messages;
  }

  async #handleServerMessage(message) {
    if (typeof message.method !== "string") {
      throw new Error(`Language server returned an unexpected response with id ${String(message.id)}`);
    }
    if (!Object.hasOwn(message, "id")) {
      const handler = this.notificationHandlers.get(message.method);
      if (handler) {
        await handler(message.params ?? {});
      }
      return;
    }

    const handler = this.requestHandlers.get(message.method);
    let response;
    if (!handler) {
      response = {
        jsonrpc: "2.0",
        id: message.id,
        error: {
          code: -32601,
          message: `Method not found: ${message.method}`
        }
      };
    } else {
      try {
        response = {
          jsonrpc: "2.0",
          id: message.id,
          result: await handler(message.params ?? {})
        };
      } catch (error) {
        console.error(`LSP client request handler failed for ${message.method}`, error);
        response = {
          jsonrpc: "2.0",
          id: message.id,
          error: {
            code: -32603,
            message: "Internal error"
          }
        };
      }
    }

    const followUpMessages = await this.#exchange(response);
    for (const followUp of followUpMessages) {
      await this.#handleServerMessage(followUp);
    }
  }
}

function requirePosition(position) {
  if (
    !position
    || !Number.isInteger(position.line)
    || position.line < 0
    || !Number.isInteger(position.character)
    || position.character < 0
  ) {
    throw new TypeError("LSP position requires non-negative line and character numbers");
  }
}

function positionAtOffset(text, offset) {
  const lineStart = text.lastIndexOf("\n", offset - 1) + 1;
  let line = 0;
  for (let index = 0; index < lineStart; index += 1) {
    if (text[index] === "\n") {
      line += 1;
    }
  }
  return { line, character: offset - lineStart };
}

function offsetAtPosition(text, position) {
  requirePosition(position);
  let line = 0;
  let offset = 0;
  while (line < position.line) {
    const lineEnd = text.indexOf("\n", offset);
    if (lineEnd === -1) {
      throw new Error("LSP position line points outside the document");
    }
    offset = lineEnd + 1;
    line += 1;
  }
  const nextLine = text.indexOf("\n", offset);
  const lineEnd = nextLine === -1 ? text.length : nextLine;
  if (position.character > lineEnd - offset) {
    throw new Error("LSP position character points outside the document line");
  }
  return offset + position.character;
}

function traceContentChanges(text, contentChanges) {
  let result = text;
  const transforms = [];
  for (const change of contentChanges) {
    if (!change || typeof change.text !== "string") {
      throw new TypeError("LSP content change requires replacement text");
    }
    if (change.range === undefined) {
      transforms.push({
        start: 0,
        end: result.length,
        insertedEnd: change.text.length
      });
      result = change.text;
      continue;
    }
    if (!change.range?.start || !change.range?.end) {
      throw new TypeError("LSP content change range requires start and end positions");
    }
    const start = offsetAtPosition(result, change.range.start);
    const end = offsetAtPosition(result, change.range.end);
    if (end < start) {
      throw new Error("LSP content change range ends before it starts");
    }
    if (change.rangeLength !== undefined && change.rangeLength !== end - start) {
      throw new Error("LSP content change range length does not match its range");
    }
    transforms.push({
      start,
      end,
      insertedEnd: start + change.text.length
    });
    result = result.slice(0, start) + change.text + result.slice(end);
  }
  return { text: result, transforms };
}

function positionBeforeContentChanges(previousText, trace, position) {
  let offset = offsetAtPosition(trace.text, position);
  for (let index = trace.transforms.length - 1; index >= 0; index -= 1) {
    const transform = trace.transforms[index];
    if (offset <= transform.start) {
      continue;
    }
    if (offset >= transform.insertedEnd) {
      offset += transform.end - transform.insertedEnd;
    } else {
      offset = transform.start;
    }
  }
  return positionAtOffset(previousText, offset);
}

function commonTextChange(previousText, nextText) {
  const previousPoints = [...previousText];
  const nextPoints = [...nextText];
  let prefixPoints = 0;
  while (
    prefixPoints < previousPoints.length
    && prefixPoints < nextPoints.length
    && previousPoints[prefixPoints] === nextPoints[prefixPoints]
  ) {
    prefixPoints += 1;
  }

  let suffixPoints = 0;
  while (
    suffixPoints < previousPoints.length - prefixPoints
    && suffixPoints < nextPoints.length - prefixPoints
    && previousPoints[previousPoints.length - 1 - suffixPoints]
      === nextPoints[nextPoints.length - 1 - suffixPoints]
  ) {
    suffixPoints += 1;
  }

  const prefixLength = previousPoints
    .slice(0, prefixPoints)
    .reduce((length, point) => length + point.length, 0);
  const previousSuffixLength = previousPoints
    .slice(previousPoints.length - suffixPoints)
    .reduce((length, point) => length + point.length, 0);
  const nextSuffixLength = nextPoints
    .slice(nextPoints.length - suffixPoints)
    .reduce((length, point) => length + point.length, 0);
  const previousEnd = previousText.length - previousSuffixLength;
  const nextEnd = nextText.length - nextSuffixLength;
  return {
    range: {
      start: positionAtOffset(previousText, prefixLength),
      end: positionAtOffset(previousText, previousEnd)
    },
    rangeLength: previousEnd - prefixLength,
    text: nextText.slice(prefixLength, nextEnd)
  };
}

class LspDocument {
  constructor(session, { uri, languageId, version, text }) {
    if (
      !session
      || typeof uri !== "string"
      || uri.length === 0
      || typeof languageId !== "string"
      || languageId.length === 0
      || !Number.isInteger(version)
      || version < 0
      || typeof text !== "string"
    ) {
      throw new TypeError("LSP text document requires a session, URI, language ID, version, and text");
    }
    this.session = session;
    this.uri = uri;
    this.languageId = languageId;
    this.version = version;
    this.text = text;
    this.closing = false;
    this.closed = false;
  }

  get identifier() {
    return { uri: this.uri };
  }

  async replaceText(text, { position, version } = {}) {
    if (this.closed) {
      throw new Error(`LSP text document is closed: ${this.uri}`);
    }
    if (typeof text !== "string") {
      throw new TypeError("LSP text document content must be a string");
    }
    if (position !== undefined) {
      requirePosition(position);
    }
    return this.session.replaceDocumentText(this, {
      version,
      text,
      position
    });
  }

  async applyChanges(contentChanges, { text, version, position } = {}) {
    if (this.closed) {
      throw new Error(`LSP text document is closed: ${this.uri}`);
    }
    if (!Array.isArray(contentChanges) || contentChanges.length === 0 || typeof text !== "string") {
      throw new TypeError("LSP document changes require content changes and the resulting text");
    }
    if (position !== undefined) {
      requirePosition(position);
    }
    return this.session.changeDocument(this, {
      version,
      text,
      contentChanges,
      position
    });
  }

  async setActiveCursor(position) {
    if (this.closed) {
      throw new Error(`LSP text document is closed: ${this.uri}`);
    }
    requirePosition(position);
    return this.session.setActiveCursor(this, position);
  }

  request(method, params = {}) {
    if (this.closed) {
      throw new Error(`LSP text document is closed: ${this.uri}`);
    }
    return this.session.request(method, {
      textDocument: this.identifier,
      ...params
    });
  }

  async close() {
    if (this.closed) {
      throw new Error(`LSP text document is not open: ${this.uri}`);
    }
    return this.session.closeDocument(this);
  }
}

export class LspSession {
  constructor(exchange, { clientId = globalThis.crypto?.randomUUID?.() } = {}) {
    if (typeof clientId !== "string" || clientId.length === 0) {
      throw new TypeError("LSP session requires a client ID");
    }
    this.client = new LspClient(exchange);
    this.clientId = clientId;
    this.documents = new Map();
    this.operationChain = Promise.resolve();
    this.state = "created";
    this.activeCursor = null;
  }

  onNotification(method, handler) {
    return this.client.onNotification(method, handler);
  }

  onRequest(method, handler) {
    return this.client.onRequest(method, handler);
  }

  async start(options = {}) {
    if (this.state !== "created") {
      throw new Error("LSP session can only be started once");
    }
    this.state = "starting";
    try {
      const result = await this.#enqueue(() => initializeLsp(this.client, options));
      this.state = "running";
      return result;
    } catch (error) {
      this.state = "failed";
      throw error;
    }
  }

  async openDocument({ uri, languageId, version = 1, text, position }) {
    this.#requireRunning();
    if (this.documents.has(uri)) {
      throw new Error(`LSP text document is already open: ${uri}`);
    }
    if (position !== undefined) {
      requirePosition(position);
    }
    const document = new LspDocument(this, { uri, languageId, version, text });
    this.documents.set(uri, document);
    try {
      await this.#enqueue(async () => {
        await this.client.notify("textDocument/didOpen", {
          textDocument: {
            uri,
            languageId,
            version,
            text
          }
        });
        if (position !== undefined) {
          await this.#notifyActiveCursor(document, position);
        }
      });
    } catch (error) {
      this.documents.delete(uri);
      throw error;
    }
    return document;
  }

  replaceDocumentText(document, { version, text, position }) {
    this.#requireOpenDocument(document);
    return this.#enqueue(async () => {
      const nextVersion = version ?? document.version + 1;
      if (!Number.isInteger(nextVersion) || nextVersion <= document.version) {
        throw new TypeError("LSP document version must increase");
      }
      if (text === document.text) {
        if (position !== undefined) {
          await this.#notifyActiveCursor(document, position);
        }
        return;
      }
      const contentChanges = [commonTextChange(document.text, text)];
      const trace = traceContentChanges(document.text, contentChanges);
      if (trace.text !== text) {
        throw new Error("Generated LSP content change does not produce the supplied document text");
      }
      if (position !== undefined) {
        const previousPosition = positionBeforeContentChanges(document.text, trace, position);
        if (this.#activeLineDiffers(document, previousPosition)) {
          await this.#notifyActiveCursor(document, previousPosition);
        }
      }
      await this.client.notify("textDocument/didChange", {
        textDocument: {
          uri: document.uri,
          version: nextVersion
        },
        contentChanges
      });
      document.version = nextVersion;
      document.text = text;
      if (position !== undefined) {
        await this.#notifyActiveCursor(document, position);
      }
    });
  }

  async changeDocument(document, { version, text, contentChanges, position }) {
    this.#requireOpenDocument(document);
    if (typeof text !== "string" || !Array.isArray(contentChanges)) {
      throw new TypeError("LSP document change requires resulting text and content changes");
    }
    if (contentChanges.length === 0) {
      if (text !== document.text) {
        throw new Error("LSP document text changed without a content change");
      }
      if (position !== undefined) {
        await this.setActiveCursor(document, position);
      }
      return;
    }
    await this.#enqueue(async () => {
      if (!Number.isInteger(version) || version <= document.version) {
        throw new TypeError("LSP document version must increase");
      }
      const trace = traceContentChanges(document.text, contentChanges);
      if (trace.text !== text) {
        throw new Error("LSP content changes do not produce the supplied document text");
      }
      if (position !== undefined) {
        const previousPosition = positionBeforeContentChanges(document.text, trace, position);
        if (this.#activeLineDiffers(document, previousPosition)) {
          await this.#notifyActiveCursor(document, previousPosition);
        }
      }
      await this.client.notify("textDocument/didChange", {
        textDocument: {
          uri: document.uri,
          version
        },
        contentChanges
      });
      document.version = version;
      document.text = text;
      if (position !== undefined) {
        await this.#notifyActiveCursor(document, position);
      }
    });
  }

  setActiveCursor(document, position) {
    this.#requireOpenDocument(document);
    requirePosition(position);
    return this.#enqueue(() => this.#notifyActiveCursor(document, position));
  }

  clearActiveCursor() {
    this.#requireRunning();
    return this.#enqueue(() => this.#notifyInactiveCursor());
  }

  request(method, params = {}) {
    this.#requireRunning();
    return this.#enqueue(() => this.client.request(method, params));
  }

  notify(method, params = {}) {
    this.#requireRunning();
    return this.#enqueue(() => this.client.notify(method, params));
  }

  async closeDocument(document) {
    this.#requireOpenDocument(document);
    document.closing = true;
    await this.#enqueue(async () => {
      if (this.activeCursor?.uri === document.uri) {
        await this.#notifyInactiveCursor();
      }
      await this.client.notify("textDocument/didClose", {
        textDocument: document.identifier
      });
      this.documents.delete(document.uri);
      document.closed = true;
    });
  }

  async stop() {
    this.#requireRunning();
    this.state = "stopping";
    try {
      await this.#enqueue(async () => {
        if (this.activeCursor !== null) {
          await this.#notifyInactiveCursor();
        }
        for (const document of this.documents.values()) {
          document.closing = true;
          await this.client.notify("textDocument/didClose", {
            textDocument: document.identifier
          });
          document.closed = true;
        }
        this.documents.clear();
        await shutdownLsp(this.client);
      });
      this.state = "stopped";
    } catch (error) {
      this.state = "failed";
      throw error;
    }
  }

  #enqueue(operation) {
    const result = this.operationChain.then(operation);
    this.operationChain = result.then(
      () => undefined,
      () => undefined
    );
    return result;
  }

  #requireRunning() {
    if (this.state !== "running") {
      throw new Error("LSP session is not running");
    }
  }

  #requireOpenDocument(document) {
    this.#requireRunning();
    if (
      !(document instanceof LspDocument)
      || this.documents.get(document.uri) !== document
      || document.closing
      || document.closed
    ) {
      throw new Error("LSP text document is not open in this session");
    }
  }

  #activeLineDiffers(document, position) {
    return (
      this.activeCursor === null
      || this.activeCursor.uri !== document.uri
      || this.activeCursor.position.line !== position.line
    );
  }

  async #notifyActiveCursor(document, position) {
    offsetAtPosition(document.text, position);
    await this.client.notify("dynlex/activeCursorChanged", {
      clientId: this.clientId,
      uri: document.uri,
      version: document.version,
      position
    });
    this.activeCursor = {
      uri: document.uri,
      position: { ...position }
    };
  }

  async #notifyInactiveCursor() {
    await this.client.notify("dynlex/activeCursorChanged", {
      clientId: this.clientId
    });
    this.activeCursor = null;
  }
}
