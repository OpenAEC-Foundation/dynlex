export class LspResponseError extends Error {
  constructor(error) {
    super(typeof error?.message === "string" ? error.message : "Language server request failed");
    this.name = "LspResponseError";
    this.code = Number.isInteger(error?.code) ? error.code : -32603;
    this.data = error?.data;
  }
}

export async function initializeLsp(client, { capabilities = {}, initializationOptions } = {}) {
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

export async function shutdownLsp(client) {
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

export class LspTextDocument {
  constructor(client, { uri, languageId }) {
    if (
      !client
      || typeof client.notify !== "function"
      || typeof uri !== "string"
      || uri.length === 0
      || typeof languageId !== "string"
      || languageId.length === 0
    ) {
      throw new TypeError("LSP text document requires a client, URI, and language ID");
    }
    this.client = client;
    this.uri = uri;
    this.languageId = languageId;
    this.version = 0;
    this.closed = false;
  }

  get identifier() {
    return { uri: this.uri };
  }

  async replaceText(text) {
    if (this.closed) {
      throw new Error(`LSP text document is closed: ${this.uri}`);
    }
    if (typeof text !== "string") {
      throw new TypeError("LSP text document content must be a string");
    }
    this.version += 1;
    if (this.version === 1) {
      await this.client.notify("textDocument/didOpen", {
        textDocument: {
          uri: this.uri,
          languageId: this.languageId,
          version: this.version,
          text
        }
      });
      return;
    }
    await this.client.notify("textDocument/didChange", {
      textDocument: {
        uri: this.uri,
        version: this.version
      },
      contentChanges: [{ text }]
    });
  }

  async close() {
    if (this.closed || this.version === 0) {
      throw new Error(`LSP text document is not open: ${this.uri}`);
    }
    await this.client.notify("textDocument/didClose", {
      textDocument: this.identifier
    });
    this.closed = true;
  }
}
