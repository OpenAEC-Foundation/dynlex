# Pattern matching LSP API

DynLex exposes two stateless custom JSON-RPC requests over its existing LSP
transport. Both operate on the pattern trees produced for an open document and
respect pattern visibility from that document.

## `dynlex/patternFrontier`

The parameters are standard `TextDocumentPositionParams`. The result contains
every distinct viable function and section matcher frontier at the position:

```json
{
  "frontiers": [
    {
      "patternKind": "function",
      "canComplete": false,
      "transitions": [
        {"kind": "literal", "text": "across"},
        {"kind": "argument"},
        {"kind": "word"}
      ]
    }
  ]
}
```

Literal text is the unconsumed source suffix. `argument` enters a recursive
function-pattern match. `word` consumes one word parameter. `canComplete`
indicates that the current line can terminate at that matcher state.

## `dynlex/filterContinuations`

This request checks multiple speculative source continuations in one operation:

```json
{
  "textDocument": {"uri": "file:///workspace/main.dl"},
  "position": {"line": 20, "character": 8},
  "continuations": [" across", [32, 98, 97, 99, 107], "\n"]
}
```

Each continuation may be a JSON string or an array of byte values. Byte arrays
allow tokenizer pieces containing incomplete UTF-8 to be checked without
altering their decoded bytes. The response identifies accepted input indices:

```json
{"accepted": [0, 1]}
```

An accepted continuation is a prefix from which at least one visible pattern
can still match. Every newline except blank lines requires the preceding line
to be complete. The last line may remain incomplete, which permits filtering
while it is generated. The request does not mutate the open document or retain
matcher state.
