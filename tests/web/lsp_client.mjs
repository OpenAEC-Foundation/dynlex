import assert from "node:assert/strict";
import path from "node:path";
import { pathToFileURL } from "node:url";

const modulePath = path.resolve(import.meta.dirname, "../../web/lsp-client.js");
const {
  initializeLsp,
  LspClient,
  LspResponseError,
  LspTextDocument,
  shutdownLsp
} = await import(pathToFileURL(modulePath).href);

const exchangedMessages = [];
const notifications = [];
const client = new LspClient(async (message) => {
  exchangedMessages.push(message);
  if (message.method === "initialize") {
    return [
      {
        jsonrpc: "2.0",
        method: "textDocument/publishDiagnostics",
        params: { uri: "file:///workspace/main.dl", diagnostics: [] }
      },
      {
        jsonrpc: "2.0",
        id: 91,
        method: "workspace/semanticTokens/refresh",
        params: {}
      },
      {
        jsonrpc: "2.0",
        id: message.id,
        result: { capabilities: { definitionProvider: true } }
      }
    ];
  }
  if (message.id === 91 && !message.method) {
    return [];
  }
  if (message.method === "textDocument/hover") {
    return [
      {
        jsonrpc: "2.0",
        id: message.id,
        error: { code: -32603, message: "Internal error" }
      }
    ];
  }
  return [];
});

client.onNotification("textDocument/publishDiagnostics", (params) => {
  notifications.push(params);
});
let semanticRefreshCount = 0;
client.onRequest("workspace/semanticTokens/refresh", () => {
  semanticRefreshCount += 1;
  return null;
});

const initializeResult = await client.request("initialize", {
  processId: null,
  rootUri: "file:///workspace"
});
assert.equal(initializeResult.capabilities.definitionProvider, true);
assert.deepEqual(notifications, [{ uri: "file:///workspace/main.dl", diagnostics: [] }]);
assert.equal(semanticRefreshCount, 1);
assert.deepEqual(exchangedMessages[0], {
  jsonrpc: "2.0",
  id: 1,
  method: "initialize",
  params: { processId: null, rootUri: "file:///workspace" }
});
assert.deepEqual(exchangedMessages[1], { jsonrpc: "2.0", id: 91, result: null });

await client.notify("initialized", {});
assert.deepEqual(exchangedMessages[2], {
  jsonrpc: "2.0",
  method: "initialized",
  params: {}
});

await assert.rejects(
  client.request("textDocument/hover", {}),
  (error) => (
    error instanceof LspResponseError
    && error.code === -32603
    && error.message === "Internal error"
  )
);

await assert.rejects(
  client.request("textDocument/definition", {}),
  /did not return a response/
);

assert.throws(
  () => new LspClient(null),
  /exchange function/
);

const lifecycleMessages = [];
const lifecycleClient = new LspClient((message) => {
  lifecycleMessages.push(message);
  if (message.method === "initialize") {
    return [{
      jsonrpc: "2.0",
      id: message.id,
      result: { capabilities: { semanticTokensProvider: { legend: { tokenTypes: [], tokenModifiers: [] } } } }
    }];
  }
  if (message.method === "shutdown") {
    return [{ jsonrpc: "2.0", id: message.id, result: null }];
  }
  return [];
});
const lifecycleResult = await initializeLsp(lifecycleClient, {
  capabilities: {
    textDocument: { semanticTokens: { requests: { full: true } } }
  },
  initializationOptions: {
    dynlex: {
      analysisProfiles: [
        { target: "spirv", shaderStage: "fragment" },
        { target: "spirv", shaderStage: "vertex" }
      ]
    }
  }
});
assert.ok(lifecycleResult.capabilities.semanticTokensProvider);
const lifecycleDocument = new LspTextDocument(lifecycleClient, {
  uri: "file:///workspace/test.dl",
  languageId: "dynlex"
});
await lifecycleDocument.replaceText("first");
await lifecycleDocument.replaceText("second");
assert.deepEqual(lifecycleDocument.identifier, { uri: "file:///workspace/test.dl" });
await lifecycleDocument.close();
await shutdownLsp(lifecycleClient);
assert.deepEqual(lifecycleMessages, [
  {
    jsonrpc: "2.0",
    id: 1,
    method: "initialize",
    params: {
      processId: null,
      rootUri: "file:///workspace",
      capabilities: {
        textDocument: { semanticTokens: { requests: { full: true } } }
      },
      initializationOptions: {
        dynlex: {
          analysisProfiles: [
            { target: "spirv", shaderStage: "fragment" },
            { target: "spirv", shaderStage: "vertex" }
          ]
        }
      }
    }
  },
  { jsonrpc: "2.0", method: "initialized", params: {} },
  {
    jsonrpc: "2.0",
    method: "textDocument/didOpen",
    params: {
      textDocument: {
        uri: "file:///workspace/test.dl",
        languageId: "dynlex",
        version: 1,
        text: "first"
      }
    }
  },
  {
    jsonrpc: "2.0",
    method: "textDocument/didChange",
    params: {
      textDocument: { uri: "file:///workspace/test.dl", version: 2 },
      contentChanges: [{ text: "second" }]
    }
  },
  {
    jsonrpc: "2.0",
    method: "textDocument/didClose",
    params: { textDocument: { uri: "file:///workspace/test.dl" } }
  },
  { jsonrpc: "2.0", id: 2, method: "shutdown", params: {} },
  { jsonrpc: "2.0", method: "exit", params: {} }
]);
await assert.rejects(lifecycleDocument.replaceText("third"), /closed/);

console.log("Browser LSP JSON-RPC client behavior is valid.");
