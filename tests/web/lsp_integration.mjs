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
const monaco = {
  Emitter: TestEmitter,
  editor: {
    registerEditorOpener: () => ({ dispose() {} })
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
  await languageFeatures.stop();
} finally {
  console.error = originalConsoleError;
}

const documentChanges = messages.filter((message) => message.method === "textDocument/didChange");
assert.equal(documentChanges.length, 1, "A transient stale cursor must not cancel document synchronization");
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
const positionedCursorMessages = messages.filter((message) => (
  message.method === "dynlex/activeCursorChanged" && message.params.uri
));
assert.deepEqual(positionedCursorMessages.at(-1).params, {
  clientId: positionedCursorMessages.at(-1).params.clientId,
  uri: "file:///workspace/repro.dl",
  version: 2,
  position: { line: 0, character: 5 }
});
assert.deepEqual(synchronizationErrors, []);

console.log("IDE document synchronization is independent from transient cursor positions.");
