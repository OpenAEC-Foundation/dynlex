---
paths:
  - "src/dap/**"
---

# DAP Architecture (`src/dap/`)

The DAP (Debug Adapter Protocol) server enables VS Code debugging of DynLex programs. It embeds in the `dynlex` binary (`--dap`), compiles the `.dl` file with `-g -O0`, launches GDB as a subprocess, and translates between DAP and GDB MI protocol.

## Components

- **`gdbmi.h/cpp`** — GDB subprocess manager. Launches GDB via `fork`/`exec` with two pipes (stdin/stdout). Includes a recursive-descent MI value parser for `"string"`, `{tuple}`, `[list]` syntax. Key methods: `send()` (async), `sendAndWait()` (sync with async callback), `readRecord()`.
- **`dapProtocol.h`** — Header-only DAP type definitions with `nlohmann::json` serialization: `Source`, `Breakpoint`, `StackFrame`, `Scope`, `Variable`, `Thread`, `Capabilities`.
- **`dapServer.h/cpp`** — Main DAP server. Reuses `lsp::Transport`/`StdioTransport` for Content-Length framing (same protocol as LSP, different message format).

## Threading model
Main thread handles DAP messages from the client. A reader thread reads GDB MI output and translates async records (`*stopped`, `@"text"`, etc.) into DAP events. Mutex on `sendJson()` prevents interleaved writes.

## Request handlers
`initialize`, `launch`, `setBreakpoints`, `configurationDone`, `threads`, `stackTrace`, `scopes`, `variables`, `continue`, `next`, `stepIn`, `stepOut`, `pause`, `disconnect`.

## Launch workflow
1. Find self via `/proc/self/exe`
2. Run `<self> <file.dl> -g -O0 -o <output>` via `fork`/`exec`
3. Launch GDB on the compiled binary with `--interpreter=mi`
4. Start reader thread for async GDB output

## GDB async → DAP events
- `*stopped,reason="breakpoint-hit"` → `stopped` event (reason: "breakpoint")
- `*stopped,reason="end-stepping-range"` → `stopped` event (reason: "step")
- `*stopped,reason="exited-normally"` → `terminated` event
- `@"text"` (target stream) → `output` event (category: "stdout")

## Name demangling
Reverses `getPatternFunctionName()` from `codegenTypes.cpp`: strips type suffixes (`_i32`, `_f64`, etc.), replaces `_` back to spaces. Heuristic — works for most pattern names.
