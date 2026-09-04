import assert from "node:assert/strict";
import { DynLexLanguageFeatures } from "../../src/web/ide/src/lspIntegration.js";

class TestEmitter {
  constructor() {
    this.listeners = new Set();
    this.event = (listener) => {
      this.listeners.add(listener);
      return { dispose: () => this.listeners.delete(listener) };
    };
  }

  fire(value) {
    for (const listener of this.listeners) {
      listener(value);
    }
  }

  dispose() {
    this.listeners.clear();
  }
}

class TestModel {
  constructor(text) {
    this.text = text;
    this.version = 1;
    this.uri = {
      path: "/workspace/repro.dl",
      toString: () => "file:///workspace/repro.dl"
    };
    this.contentListeners = new Set();
  }

  getLanguageId() {
    return "dynlex";
  }

  getValue() {
    return this.text;
  }

  getVersionId() {
    return this.version;
  }

  onDidChangeContent(listener) {
    this.contentListeners.add(listener);
    return { dispose: () => this.contentListeners.delete(listener) };
  }

  replaceTextBeforeCursorSettles(text, change) {
    this.text = text;
    this.version += 1;
    for (const listener of this.contentListeners) {
      listener({ changes: [change] });
    }
  }

  replaceTextAfterCursorMoves(text, editor, position, changes) {
    this.text = text;
    this.version += 1;
    editor.settleCursor(position);
    for (const listener of this.contentListeners) {
      listener({ changes });
    }
  }
}

class TestEditor {
  constructor(model, position) {
    this.model = model;
    this.position = position;
    this.cursorListeners = new Set();
    this.modelListeners = new Set();
  }

  getModel() {
    return this.model;
  }

  getPosition() {
    return this.position;
  }

  onDidChangeCursorPosition(listener) {
    this.cursorListeners.add(listener);
    return { dispose: () => this.cursorListeners.delete(listener) };
  }

  onDidChangeModel(listener) {
    this.modelListeners.add(listener);
    return { dispose: () => this.modelListeners.delete(listener) };
  }

  updateOptions() {}

  settleCursor(position) {
    this.position = position;
    for (const listener of this.cursorListeners) {
      listener({ position });
    }
  }
}

const model = new TestModel("one\ntwo\nthree");
const editor = new TestEditor(model, { lineNumber: 3, column: 6 });
const messages = [];
const markerUpdates = [];
const monaco = {
  Emitter: TestEmitter,
  MarkerSeverity: { Error: 8, Warning: 4, Info: 2, Hint: 1 },
  editor: {
    registerEditorOpener: () => ({ dispose() {} }),
    getModels: () => [model],
    setModelMarkers: (markedModel, owner, markers) => markerUpdates.push({ markedModel, owner, markers })
  },
  languages: {}
};
const exchange = async (message) => {
  messages.push(message);
  if (message.method === "initialize") {
    return [{
      jsonrpc: "2.0",
      id: message.id,
      result: { capabilities: {} }
    }];
  }
  if (message.method === "shutdown") {
    return [{ jsonrpc: "2.0", id: message.id, result: null }];
  }
  if (message.method === "textDocument/didOpen") {
    return [{
      jsonrpc: "2.0",
      method: "textDocument/publishDiagnostics",
      params: {
        uri: "file:///lib/std.dl",
        diagnostics: [{
          range: { start: { line: 18, character: 8 }, end: { line: 18, character: 25 } },
          severity: 1,
          message: "the assigned value has no type",
          relatedInformation: [{
            location: {
              uri: model.uri.toString(),
              range: { start: { line: 2, character: 0 }, end: { line: 2, character: 5 } }
            },
            message: "while inferring the caller"
          }]
        }]
      }
    }];
  }
  return [];
};

const languageFeatures = new DynLexLanguageFeatures({
  monaco,
  editor,
  mainModel: model,
  exchange,
  analysisProfiles: [{ target: "cpu" }]
});
const synchronizationErrors = [];
const originalConsoleError = console.error;
console.error = (...values) => synchronizationErrors.push(values);
try {
  await languageFeatures.start();
  model.replaceTextBeforeCursorSettles("short", {
    range: {
      startLineNumber: 1,
      startColumn: 1,
      endLineNumber: 3,
      endColumn: 6
    },
    rangeLength: 13,
    text: "short"
  });
  editor.settleCursor({ lineNumber: 1, column: 6 });
  model.replaceTextAfterCursorMoves(
    "shorter",
    editor,
    { lineNumber: 1, column: 8 },
    [{
      range: {
        startLineNumber: 99,
        startColumn: 99,
        endLineNumber: 99,
        endColumn: 99
      },
      rangeLength: 0,
      text: "ignored incremental coordinates"
    }]
  );
  model.replaceTextBeforeCursorSettles("shortest", {
    range: {
      startLineNumber: 1,
      startColumn: 6,
      endLineNumber: 1,
      endColumn: 8
    },
    rangeLength: 2,
    text: "est"
  });
  editor.settleCursor({ lineNumber: 1, column: 9 });
  await languageFeatures.commitActiveLine();
  await languageFeatures.stop();
} finally {
  console.error = originalConsoleError;
}

const documentChanges = messages.filter((message) => message.method === "textDocument/didChange");
assert.equal(documentChanges.length, 3, "Cursor-first edits must not poison subsequent synchronization");
assert.deepEqual(documentChanges[0].params, {
  textDocument: {
    uri: "file:///workspace/repro.dl",
    version: 2
  },
  contentChanges: [{
    range: {
      start: { line: 0, character: 0 },
      end: { line: 2, character: 5 }
    },
    rangeLength: 13,
    text: "short"
  }]
});
assert.deepEqual(documentChanges[1].params, {
  textDocument: {
    uri: "file:///workspace/repro.dl",
    version: 3
  },
  contentChanges: [{
    range: {
      start: { line: 0, character: 5 },
      end: { line: 0, character: 5 }
    },
    rangeLength: 0,
    text: "er"
  }]
});
assert.deepEqual(documentChanges[2].params, {
  textDocument: {
    uri: "file:///workspace/repro.dl",
    version: 4
  },
  contentChanges: [{
    range: {
      start: { line: 0, character: 6 },
      end: { line: 0, character: 7 }
    },
    rangeLength: 1,
    text: "st"
  }]
});
const positionedCursorMessages = messages.filter((message) => (
  message.method === "dynlex/activeCursorChanged" && message.params.uri
));
assert.deepEqual(positionedCursorMessages.at(-1).params, {
  clientId: positionedCursorMessages.at(-1).params.clientId,
  uri: "file:///workspace/repro.dl",
  version: 4,
  position: { line: 0, character: 8 }
});
const cursorMessages = messages.filter((message) => message.method === "dynlex/activeCursorChanged");
assert.deepEqual(cursorMessages.slice(-3, -1).map((message) => message.params.uri ?? null), [null, model.uri.toString()]);
assert.deepEqual(synchronizationErrors, []);
assert.deepEqual(markerUpdates[0], {
  markedModel: model,
  owner: "dynlex-lsp",
  markers: [{
    startLineNumber: 3,
    startColumn: 1,
    endLineNumber: 3,
    endColumn: 6,
    severity: monaco.MarkerSeverity.Error,
    message: "while inferring the caller",
    source: "dynlex"
  }]
});

console.log("IDE document synchronization is independent from transient cursor positions.");
