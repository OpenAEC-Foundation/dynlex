import assert from "node:assert/strict";
import path from "node:path";
import { pathToFileURL } from "node:url";

const modulePath = path.resolve(import.meta.dirname, "../../web/semantic-highlighting.js");
const {
  decodeSemanticTokenRanges,
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
assert.throws(
  () => decodeSemanticTokenRanges("set", [0, 0, 4, 0, 0], legend),
  /invalid range or type/
);

console.log("Shared semantic highlighting is valid.");
