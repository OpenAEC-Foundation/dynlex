import assert from "node:assert/strict";
import path from "node:path";
import { pathToFileURL } from "node:url";

const modulePath = path.resolve(import.meta.dirname, "../../web/lsp-client.js");
const {
  LspClient,
  LspResponseError,
  LspSession
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
const lifecycleSession = new LspSession((message) => {
  lifecycleMessages.push(message);
  if (message.method === "initialize") {
    return [{
      jsonrpc: "2.0",
      id: message.id,
      result: { capabilities: { semanticTokensProvider: { legend: { tokenTypes: [], tokenModifiers: [] } } } }
    }];
  }
  if (message.method === "textDocument/completion") {
    return [{
      jsonrpc: "2.0",
      id: message.id,
      result: {
        isIncomplete: false,
        items: [{ label: "sheep", insertText: "sheep" }]
      }
    }];
  }
  if (message.method === "shutdown") {
    return [{ jsonrpc: "2.0", id: message.id, result: null }];
  }
  return [];
}, { clientId: "browser-editor" });
const lifecycleResult = await lifecycleSession.start({
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
const lifecycleDocument = await lifecycleSession.openDocument({
  uri: "file:///workspace/test.dl",
  languageId: "dynlex",
  version: 1,
  text: "first\nsecond",
  position: { line: 0, character: 5 }
});
await lifecycleDocument.replaceText("first\nsecond sheep", {
  position: { line: 1, character: 12 }
});
await lifecycleDocument.applyChanges([
  {
    range: {
      start: { line: 0, character: 5 },
      end: { line: 0, character: 5 }
    },
    rangeLength: 0,
    text: " wolf"
  }
], {
  text: "first wolf\nsecond sheep",
  version: 3,
  position: { line: 0, character: 10 }
});
assert.deepEqual(lifecycleDocument.identifier, { uri: "file:///workspace/test.dl" });
const completions = await lifecycleDocument.request("textDocument/completion", {
  position: { line: 1, character: 12 }
});
assert.equal(completions.items[0].label, "sheep");
await lifecycleDocument.close();
await lifecycleSession.stop();
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
        text: "first\nsecond"
      }
    }
  },
  {
    jsonrpc: "2.0",
    method: "dynlex/activeCursorChanged",
    params: {
      clientId: "browser-editor",
      uri: "file:///workspace/test.dl",
      version: 1,
      position: { line: 0, character: 5 }
    }
  },
  {
    jsonrpc: "2.0",
    method: "dynlex/activeCursorChanged",
    params: {
      clientId: "browser-editor",
      uri: "file:///workspace/test.dl",
      version: 1,
      position: { line: 1, character: 6 }
    }
  },
  {
    jsonrpc: "2.0",
    method: "textDocument/didChange",
    params: {
      textDocument: { uri: "file:///workspace/test.dl", version: 2 },
      contentChanges: [{
        range: {
          start: { line: 1, character: 6 },
          end: { line: 1, character: 6 }
        },
        rangeLength: 0,
        text: " sheep"
      }]
    }
  },
  {
    jsonrpc: "2.0",
    method: "dynlex/activeCursorChanged",
    params: {
      clientId: "browser-editor",
      uri: "file:///workspace/test.dl",
      version: 2,
      position: { line: 1, character: 12 }
    }
  },
  {
    jsonrpc: "2.0",
    method: "dynlex/activeCursorChanged",
    params: {
      clientId: "browser-editor",
      uri: "file:///workspace/test.dl",
      version: 2,
      position: { line: 0, character: 5 }
    }
  },
  {
    jsonrpc: "2.0",
    method: "textDocument/didChange",
    params: {
      textDocument: { uri: "file:///workspace/test.dl", version: 3 },
      contentChanges: [{
        range: {
          start: { line: 0, character: 5 },
          end: { line: 0, character: 5 }
        },
        rangeLength: 0,
        text: " wolf"
      }]
    }
  },
  {
    jsonrpc: "2.0",
    method: "dynlex/activeCursorChanged",
    params: {
      clientId: "browser-editor",
      uri: "file:///workspace/test.dl",
      version: 3,
      position: { line: 0, character: 10 }
    }
  },
  {
    jsonrpc: "2.0",
    id: 2,
    method: "textDocument/completion",
    params: {
      textDocument: { uri: "file:///workspace/test.dl" },
      position: { line: 1, character: 12 }
    }
  },
  {
    jsonrpc: "2.0",
    method: "dynlex/activeCursorChanged",
    params: { clientId: "browser-editor" }
  },
  {
    jsonrpc: "2.0",
    method: "textDocument/didClose",
    params: { textDocument: { uri: "file:///workspace/test.dl" } }
  },
  { jsonrpc: "2.0", id: 3, method: "shutdown", params: {} },
  { jsonrpc: "2.0", method: "exit", params: {} }
]);
await assert.rejects(lifecycleDocument.replaceText("third"), /closed/);

console.log("Browser LSP JSON-RPC client behavior is valid.");
