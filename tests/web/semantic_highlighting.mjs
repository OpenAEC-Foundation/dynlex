import assert from "node:assert/strict";
import path from "node:path";
import { pathToFileURL } from "node:url";

const modulePath = path.resolve(import.meta.dirname, "../../web/semantic-highlighting.js");
const {
  decodeSemanticTokenRanges,
  rebaseLspDiagnosticsAfterLines,
  rebaseSemanticTokensAfterLines,
  semanticLegendsMatch,
  semanticTokenClassName
} = await import(pathToFileURL(modulePath).href);

const legend = {
  tokenTypes: ["keyword", "variable"],
  tokenModifiers: ["definition"]
};
const matchingLegend = {
  tokenTypes: ["keyword", "variable"],
  tokenModifiers: ["definition"]
};

assert.equal(semanticLegendsMatch(legend, matchingLegend), true);
assert.equal(semanticLegendsMatch(legend, { ...matchingLegend, tokenTypes: ["keyword"] }), false);
assert.deepEqual(
  decodeSemanticTokenRanges("set glow\nset hue", [0, 0, 3, 0, 0, 0, 4, 4, 1, 0, 1, 0, 3, 0, 0], legend),
  [
    { start: 0, end: 3, tokenType: "keyword" },
    { start: 4, end: 8, tokenType: "variable" },
    { start: 9, end: 12, tokenType: "keyword" }
  ]
);
assert.equal(semanticTokenClassName("patternDefinition", "token-"), "token-patterndefinition");
assert.deepEqual(
  rebaseSemanticTokensAfterLines([
    0, 0, 6, 0, 0,
    2, 0, 3, 1, 0,
    0, 8, 5, 1, 0,
    1, 0, 3, 1, 0
  ], 2),
  [
    0, 0, 3, 1, 0,
    0, 8, 5, 1, 0,
    1, 0, 3, 1, 0
  ]
);
assert.deepEqual(
  rebaseLspDiagnosticsAfterLines([
    {
      range: {
        start: { line: 3, character: 0 },
        end: { line: 3, character: 13 }
      },
      severity: 1,
      message: "unterminated string"
    }
  ], 2),
  [
    {
      range: {
        start: { line: 1, character: 0 },
        end: { line: 1, character: 13 }
      },
      severity: 1,
      message: "unterminated string"
    }
  ]
);
assert.throws(
  () => rebaseLspDiagnosticsAfterLines([
    {
      range: {
        start: { line: 0, character: 0 },
        end: { line: 0, character: 6 }
      },
      severity: 1,
      message: "import failed"
    }
  ], 2),
  /removed prefix/
);
assert.throws(
  () => decodeSemanticTokenRanges("set", [0, 0, 4, 0, 0], legend),
  /invalid range or type/
);

console.log("Shared semantic highlighting is valid.");
