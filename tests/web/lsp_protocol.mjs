import assert from "node:assert/strict";
import path from "node:path";
import { pathToFileURL } from "node:url";

const modulePath = path.resolve(import.meta.dirname, "../../src/web/ide/src/lspProtocol.js");
const {
  completionKindFromLsp,
  diagnosticSeverityFromLsp,
  positionFromLsp,
  positionToLsp,
  rangeFromLsp,
  rangeToLsp,
  symbolKindFromLsp
} = await import(pathToFileURL(modulePath).href);

assert.deepEqual(positionToLsp({ lineNumber: 4, column: 9 }), { line: 3, character: 8 });
assert.deepEqual(positionFromLsp({ line: 3, character: 8 }), { lineNumber: 4, column: 9 });
assert.deepEqual(
  rangeToLsp({
    startLineNumber: 2,
    startColumn: 3,
    endLineNumber: 5,
    endColumn: 7
  }),
  {
    start: { line: 1, character: 2 },
    end: { line: 4, character: 6 }
  }
);
assert.deepEqual(
  rangeFromLsp({
    start: { line: 1, character: 2 },
    end: { line: 4, character: 6 }
  }),
  {
    startLineNumber: 2,
    startColumn: 3,
    endLineNumber: 5,
    endColumn: 7
  }
);

const completionKinds = {
  Text: 18,
  Method: 0,
  Function: 1,
  Variable: 4,
  Module: 8,
  Keyword: 17,
  Snippet: 27,
  File: 20
};
assert.equal(completionKindFromLsp(1, completionKinds), completionKinds.Text);
assert.equal(completionKindFromLsp(2, completionKinds), completionKinds.Method);
assert.equal(completionKindFromLsp(3, completionKinds), completionKinds.Function);
assert.equal(completionKindFromLsp(6, completionKinds), completionKinds.Variable);
assert.equal(completionKindFromLsp(9, completionKinds), completionKinds.Module);
assert.equal(completionKindFromLsp(14, completionKinds), completionKinds.Keyword);
assert.equal(completionKindFromLsp(15, completionKinds), completionKinds.Snippet);
assert.equal(completionKindFromLsp(17, completionKinds), completionKinds.File);
assert.equal(completionKindFromLsp(undefined, completionKinds), completionKinds.Text);

const symbolKinds = {
  File: 0,
  Module: 1,
  Namespace: 2,
  Class: 4,
  Function: 11,
  Variable: 12,
  Operator: 24
};
assert.equal(symbolKindFromLsp(1, symbolKinds), symbolKinds.File);
assert.equal(symbolKindFromLsp(2, symbolKinds), symbolKinds.Module);
assert.equal(symbolKindFromLsp(3, symbolKinds), symbolKinds.Namespace);
assert.equal(symbolKindFromLsp(5, symbolKinds), symbolKinds.Class);
assert.equal(symbolKindFromLsp(12, symbolKinds), symbolKinds.Function);
assert.equal(symbolKindFromLsp(13, symbolKinds), symbolKinds.Variable);
assert.equal(symbolKindFromLsp(25, symbolKinds), symbolKinds.Operator);
assert.equal(symbolKindFromLsp(undefined, symbolKinds), symbolKinds.Namespace);

const markerSeverities = { Error: 8, Warning: 4, Info: 2, Hint: 1 };
assert.equal(diagnosticSeverityFromLsp(1, markerSeverities), markerSeverities.Error);
assert.equal(diagnosticSeverityFromLsp(2, markerSeverities), markerSeverities.Warning);
assert.equal(diagnosticSeverityFromLsp(3, markerSeverities), markerSeverities.Info);
assert.equal(diagnosticSeverityFromLsp(4, markerSeverities), markerSeverities.Hint);
assert.equal(diagnosticSeverityFromLsp(undefined, markerSeverities), markerSeverities.Info);

for (const invalidPosition of [
  null,
  {},
  { line: -1, character: 0 },
  { line: 0, character: -1 }
]) {
  assert.throws(() => positionFromLsp(invalidPosition), /valid non-negative position/);
}

console.log("Browser LSP-to-Monaco protocol conversions are valid.");
