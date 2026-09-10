import assert from "node:assert/strict";
import path from "node:path";
import { pathToFileURL } from "node:url";

const modulePath = path.resolve(import.meta.dirname, "../../web/semantic-highlighting.js");
const {
  decodeSemanticTokenRanges,
  rebaseSemanticTokensAfterLines,
  semanticTokenClassName
} = await import(pathToFileURL(modulePath).href);

const legend = {
  tokenTypes: ["keyword", "variable"],
  tokenModifiers: ["definition"]
};


assert.deepEqual(
  decodeSemanticTokenRanges("set glow\nset hue", [0, 0, 3, 0, 0, 0, 4, 4, 1, 0, 1, 0, 3, 0, 0], legend),
  [
    { start: 0, end: 3, tokenType: "keyword", modifiers: [] },
    { start: 4, end: 8, tokenType: "variable", modifiers: [] },
    { start: 9, end: 12, tokenType: "keyword", modifiers: [] }
  ]
);
assert.deepEqual(decodeSemanticTokenRanges("show one", [0, 0, 4, 0, 1, 0, 5, 3, 0, 2], {
  tokenTypes: ["function"], tokenModifiers: ["callDepth0", "callDepth1"]
}), [
  { start: 0, end: 4, tokenType: "function", modifiers: ["callDepth0"] },
  { start: 5, end: 8, tokenType: "function", modifiers: ["callDepth1"] }
]);
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
console.log("Shared semantic highlighting is valid.");
