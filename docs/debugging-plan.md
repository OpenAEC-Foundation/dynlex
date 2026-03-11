# DynLex Debugging Support — Implementation Plan

## Overview

Add step-debugging support for DynLex programs: breakpoints, stepping, variable inspection, and call stacks — all integrated into VS Code via the Debug Adapter Protocol (DAP).

**Three phases:**
1. Emit DWARF debug info from the compiler (`-g` flag)
2. Build a DAP server into the `dynlex` binary (reusing existing transport infrastructure)
3. Integrate into the VS Code extension (launch configs, breakpoint contribution, debug adapter registration)

---

## Phase 1: DWARF Debug Info Emission

### Goal
When compiling with `-g`, emit DWARF metadata so that any debugger (GDB/LLDB) can map machine instructions back to `.dl` source lines.

### 1.1 Add `-g` CLI Flag

**File:** `src/main.cpp`

Add a `--debug` / `-g` flag that sets `context.options.emitDebugInfo = true`.

**File:** `src/compiler/parseContext.h`

Add to `Options`:
```cpp
bool emitDebugInfo = false;
```

### 1.2 Add `DIBuilder` to ParseContext

**File:** `src/compiler/parseContext.h`

Add fields:
```cpp
llvm::DIBuilder    *llvmDIBuilder{};
llvm::DICompileUnit *llvmDICompileUnit{};
// Map source file URI → DIFile (one per imported file)
std::unordered_map<std::string, llvm::DIFile *> debugFiles;
// Current debug scope stack (functions push/pop DISubprogram)
llvm::DIScope *currentDebugScope{};
```

### 1.3 Initialize Debug Info in `generateCode`

**File:** `src/compiler/codegen/codegen.cpp`, in `generateCode()` after module creation (~line 335)

```cpp
if (context.options.emitDebugInfo) {
    context.llvmDIBuilder = new llvm::DIBuilder(*context.llvmModule);

    // Convert input file URI to filesystem path
    std::string mainFilePath = uriToPath(context.options.inputPath);
    std::string directory = std::filesystem::path(mainFilePath).parent_path().string();
    std::string filename  = std::filesystem::path(mainFilePath).filename().string();

    llvm::DIFile *mainFile = context.llvmDIBuilder->createFile(filename, directory);
    context.debugFiles[context.options.inputPath] = mainFile;

    context.llvmDICompileUnit = context.llvmDIBuilder->createCompileUnit(
        llvm::dwarf::DW_LANG_C,  // closest match; no custom DWARF lang ID needed
        mainFile,
        "DynLex Compiler",       // producer
        context.options.optimizationLevel > 0,  // isOptimized
        "",                      // flags
        0                        // runtime version
    );

    // Disable optimizations that break debug stepping if -g is used
    // (or let user combine -g with -O, their choice)
}
```

After all codegen, before `verifyModule`:
```cpp
if (context.llvmDIBuilder)
    context.llvmDIBuilder->finalize();
```

### 1.4 Helper: Get or Create DIFile for a Source File

**File:** `src/compiler/codegen/codegenTypes.cpp` (or a new `codegenDebug.cpp`)

```cpp
llvm::DIFile *getOrCreateDIFile(ParseContext &context, lsp::SourceFile *sourceFile) {
    if (!context.llvmDIBuilder) return nullptr;
    auto it = context.debugFiles.find(sourceFile->uri);
    if (it != context.debugFiles.end()) return it->second;

    std::string path = uriToPath(sourceFile->uri);
    std::string dir  = std::filesystem::path(path).parent_path().string();
    std::string file = std::filesystem::path(path).filename().string();
    auto *diFile = context.llvmDIBuilder->createFile(file, dir);
    context.debugFiles[sourceFile->uri] = diFile;
    return diFile;
}
```

### 1.5 Attach Debug Locations to Instructions

**File:** `src/compiler/codegen/codegen.cpp`

**In `generateSectionCode`**, before each function-codegen call:
```cpp
if (context.llvmDIBuilder && line->sourceFile) {
    auto *diFile = getOrCreateDIFile(context, line->sourceFile);
    auto loc = llvm::DILocation::get(
        *context.llvmContext,
        line->sourceFileLineIndex + 1,  // DWARF lines are 1-based
        0,                               // column (0 = unknown)
        context.currentDebugScope ? context.currentDebugScope : context.llvmDICompileUnit
    );
    context.llvmBuilder->SetCurrentDebugLocation(loc);
}
```

**In `generateFunctionCode`**, for finer-grained column info (optional but nice for single-line multi-call stepping):
```cpp
if (context.llvmDIBuilder && expr->range.line) {
    auto *diFile = getOrCreateDIFile(context, expr->range.line->sourceFile);
    auto loc = llvm::DILocation::get(
        *context.llvmContext,
        expr->range.line->sourceFileLineIndex + 1,
        expr->range.start() + 1,  // DWARF columns are 1-based
        context.currentDebugScope ? context.currentDebugScope : context.llvmDICompileUnit
    );
    context.llvmBuilder->SetCurrentDebugLocation(loc);
}
```

### 1.6 Create DISubprogram for Functions

**File:** `src/compiler/codegen/codegen.cpp`, in `generateSpecializedFunction`

After creating the `llvm::Function` (~line 62), before generating the body:
```cpp
if (context.llvmDIBuilder) {
    auto *diFile = getOrCreateDIFile(context, section->codeLines[0]->sourceFile);
    unsigned lineNo = section->codeLines[0]->sourceFileLineIndex + 1;

    llvm::DISubroutineType *debugFuncType =
        context.llvmDIBuilder->createSubroutineType(
            context.llvmDIBuilder->getOrCreateTypeArray(std::nullopt)
        );

    llvm::DISubprogram *sp = context.llvmDIBuilder->createFunction(
        diFile,                          // scope (file)
        inst.llvmFunction->getName(),    // name (e.g., "add_i32_i32")
        inst.llvmFunction->getName(),    // linkage name
        diFile,                          // file
        lineNo,                          // line
        debugFuncType,                   // type
        lineNo,                          // scope line
        llvm::DINode::FlagPrototyped,    // flags
        llvm::DISubprogram::SPFlagDefinition  // spFlags
    );
    inst.llvmFunction->setSubprogram(sp);
    context.currentDebugScope = sp;
}
```

Save and restore `currentDebugScope` alongside other saved state (~lines 69-90).

### 1.7 Create DISubprogram for `main`

Same pattern as above, but for the `main` function created in `generateCode` (~line 370). Set `context.currentDebugScope = mainSP` before `generateSectionCode(context, context.mainSection)`.

### 1.8 Debug Info for Variables (for variable inspection)

In `allocateSectionVariables` (codegenTypes.cpp), after creating each `alloca`:
```cpp
if (context.llvmDIBuilder && var->definition && var->definition->range.line) {
    auto *diFile = getOrCreateDIFile(context, var->definition->range.line->sourceFile);
    unsigned line = var->definition->range.line->sourceFileLineIndex + 1;
    unsigned col  = var->definition->range.start() + 1;

    llvm::DILocalVariable *diVar = context.llvmDIBuilder->createAutoVariable(
        context.currentDebugScope,
        var->name,
        diFile,
        line,
        getDIType(context, var->type)  // helper needed
    );
    context.llvmDIBuilder->insertDeclare(
        var->alloca, diVar,
        context.llvmDIBuilder->create debug info metadata,
        llvm::DILocation::get(*context.llvmContext, line, col, context.currentDebugScope),
        context.llvmBuilder->GetInsertBlock()
    );
}
```

**Helper `getDIType`** maps DynLex `Type` → `llvm::DIType *`:
- `i32` → `createBasicType("i32", 32, dwarf::DW_ATE_signed)`
- `f64` → `createBasicType("f64", 64, dwarf::DW_ATE_float)`
- `bool` → `createBasicType("bool", 1, dwarf::DW_ATE_boolean)`
- Classes → `createStructType(...)` with member layout
- Pointers → `createPointerType(...)`

### 1.9 Preserve Debug Sections in Object Emission

**File:** `src/compiler/codegen/native.cpp`

The existing `legacy::PassManager` + `addPassesToEmitFile` already preserves DWARF sections in the `.o` file — no change needed there.

But the link command needs `-g` so the linker preserves debug info:
```cpp
if (context.options.emitDebugInfo)
    linkCommand += " -g";
```

### 1.10 Disable `-g` for SPIR-V

SPIR-V doesn't use DWARF. In `generateCode`, skip all debug info setup when `emitSPIRV` is true, regardless of the `-g` flag.

---

## Phase 2: DAP Server

### Architecture Decision

**Approach: Embed a DAP server in the `dynlex` binary** that controls the debuggee process via GDB's Machine Interface (MI) protocol.

Why:
- Reuses existing transport infrastructure (`Transport`, `tcpTransport`, `stdioTransport`)
- Single binary — no separate debug adapter to install
- Can present DynLex-aware views (pattern names instead of mangled function names, macro expansion tracking)
- GDB is pre-installed on most Linux distributions; LLDB on macOS

The DAP server translates between VS Code's DAP requests and GDB/MI commands, mapping DWARF-level concepts back to DynLex source constructs.

### 2.1 CLI Entry Point

**File:** `src/main.cpp`

Add `--dap` flag (and `--dap-stdio`):
```
./build/dynlex --dap          # DAP server on TCP (port 5008)
./build/dynlex --dap --stdio  # DAP server on stdio (for VS Code)
```

### 2.2 DAP Server Class

**New files:** `src/dap/dapServer.h`, `src/dap/dapServer.cpp`

```cpp
class DapServer {
    std::unique_ptr<lsp::Transport> transport;
    // GDB MI subprocess
    pid_t gdbPid = -1;
    int gdbStdinFd = -1;
    int gdbStdoutFd = -1;
    // State
    std::string programPath;
    std::unordered_map<std::string, std::vector<int>> breakpoints; // file → lines
    bool isRunning = false;

public:
    DapServer(std::unique_ptr<lsp::Transport> transport);
    void run();  // main message loop (same pattern as LanguageServer::run)

private:
    // DAP request handlers
    json handleInitialize(const json &args);
    json handleLaunch(const json &args);
    json handleSetBreakpoints(const json &args);
    json handleConfigurationDone(const json &args);
    json handleThreads(const json &args);
    json handleStackTrace(const json &args);
    json handleScopes(const json &args);
    json handleVariables(const json &args);
    json handleContinue(const json &args);
    json handleNext(const json &args);       // step over
    json handleStepIn(const json &args);
    json handleStepOut(const json &args);
    json handlePause(const json &args);
    json handleDisconnect(const json &args);
    json handleEvaluate(const json &args);   // watch values

    // GDB MI communication
    void launchGdb(const std::string &program);
    std::string sendMiCommand(const std::string &cmd);
    void parseMiOutput(const std::string &line);

    // DynLex-aware mapping
    std::string demanglePatternName(const std::string &llvmName);
    // Maps monomorphized function names back to pattern definitions
    // e.g., "add_i32_i32" → "function left + right"
};
```

### 2.3 DAP Protocol Messages

The DAP protocol uses JSON messages with `Content-Length` headers — identical framing to LSP. The existing `LanguageServer` base class's read/write loop can be reused or extracted.

**Key DAP message flow:**

```
VS Code                         DapServer                      GDB (MI)
  │                                │                              │
  ├─ initialize ──────────────────►│                              │
  │◄──────────── capabilities ─────┤                              │
  │                                │                              │
  ├─ launch {program, args} ──────►│── -file-exec-and-symbols ──►│
  │                                │── -exec-arguments ─────────►│
  │                                │                              │
  ├─ setBreakpoints ──────────────►│── -break-insert file:line ─►│
  │◄──────── breakpoint locations ─┤                              │
  │                                │                              │
  ├─ configurationDone ───────────►│── -exec-run ───────────────►│
  │                                │                              │
  │                                │◄── *stopped (breakpoint) ───┤
  │◄──── stopped event ───────────┤                              │
  │                                │                              │
  ├─ stackTrace ──────────────────►│── -stack-list-frames ──────►│
  │◄──── frames (DynLex names) ───┤   (demangle pattern names)  │
  │                                │                              │
  ├─ variables ───────────────────►│── -stack-list-variables ───►│
  │◄──── vars (DynLex names) ─────┤   (map DWARF names)         │
  │                                │                              │
  ├─ continue ────────────────────►│── -exec-continue ──────────►│
```

### 2.4 GDB MI Wrapper

**New file:** `src/dap/gdbmi.h`, `src/dap/gdbmi.cpp`

Manages the GDB subprocess:
```cpp
class GdbMI {
    pid_t pid;
    int stdinFd, stdoutFd;
    int nextToken = 1;

public:
    void launch(const std::string &gdbPath);
    void terminate();

    // Send MI command, return token for async matching
    int send(const std::string &command);

    // Read and parse MI output records
    struct MiRecord {
        enum Type { Result, Async, Stream } type;
        int token;           // -1 if no token
        std::string klass;   // "done", "stopped", "running", etc.
        json payload;        // parsed key=value pairs
    };
    MiRecord readRecord();

    // Convenience wrappers
    MiRecord execAndWait(const std::string &command);
};
```

### 2.5 DynLex-Aware Features (Customizations)

These are why we build a custom adapter instead of using CodeLLDB:

1. **Pattern name demangling**: Stack frames show `function left + right` instead of `add_i32_i32`. The DAP server maintains a map from monomorphized names → pattern definition text (built during compilation or by parsing the binary's symbol table + the source).

2. **Macro expansion transparency**: When stopped inside a macro body, the stack trace shows both the macro expansion site and the macro definition, with the source location pointing to the caller's line (not the macro body).

3. **Variable display names**: DWARF variable names will be the DynLex variable names (since we control debug info emission), but the adapter can add type annotations in DynLex syntax (e.g., `x: 32 bit integer = 42`).

4. **Compile-and-debug workflow**: The `launch` request can accept a `.dl` file directly. The DAP server compiles it with `-g`, then launches the resulting binary under GDB. Single-click debugging.

5. **Breakpoint validation**: The DAP server can validate breakpoints against the compiled debug info and report which breakpoints are on valid lines.

6. **Conditional breakpoints with DynLex functions**: (Future) Translate DynLex function syntax to equivalent GDB conditions.

### 2.6 Finding a Debugger Backend

On launch, the DAP server searches for a debugger in order:
1. `gdb` on PATH (most common on Linux)
2. `lldb-mi` on PATH (LLDB's MI-compatible interface)
3. Bundled `gdb` shipped with the VS Code extension (see Phase 3)

If none found, send an error event with a helpful message.

---

## Phase 3: VS Code Extension Integration

### 3.1 Package.json — Debugger Contribution

**File:** `vscode-extension/package.json`

Add to `contributes`:
```json
{
  "breakpoints": [
    { "language": "dynlex" }
  ],
  "debuggers": [
    {
      "type": "dynlex",
      "label": "DynLex Debug",
      "program": "",
      "runtime": "",
      "languages": ["dynlex"],
      "configurationAttributes": {
        "launch": {
          "required": ["program"],
          "properties": {
            "program": {
              "type": "string",
              "description": "Path to the .dl file to debug",
              "default": "${file}"
            },
            "args": {
              "type": "array",
              "description": "Command-line arguments for the program",
              "default": []
            },
            "compilerPath": {
              "type": "string",
              "description": "Path to the dynlex compiler",
              "default": ""
            },
            "cwd": {
              "type": "string",
              "description": "Working directory",
              "default": "${workspaceFolder}"
            },
            "stopOnEntry": {
              "type": "boolean",
              "description": "Break on program entry",
              "default": false
            }
          }
        }
      },
      "configurationSnippets": [
        {
          "label": "DynLex: Debug Current File",
          "description": "Compile and debug the current .dl file",
          "body": {
            "type": "dynlex",
            "request": "launch",
            "name": "Debug DynLex",
            "program": "${file}"
          }
        }
      ]
    }
  ]
}
```

### 3.2 Debug Adapter Descriptor Factory

**File:** `vscode-extension/src/extension.ts`

Register a debug adapter factory that launches `dynlex --dap --stdio`:

```typescript
class DynLexDebugAdapterFactory implements vscode.DebugAdapterDescriptorFactory {
    createDebugAdapterDescriptor(
        session: vscode.DebugSession
    ): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
        const dynlexPath = resolveDynlexBinary(); // existing helper
        return new vscode.DebugAdapterExecutable(dynlexPath, ['--dap', '--stdio'], {
            cwd: session.workspaceFolder?.uri.fsPath
        });
    }
}

// In activate():
context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory(
        'dynlex',
        new DynLexDebugAdapterFactory()
    )
);
```

### 3.3 Debug Configuration Provider (optional but nice)

Auto-generate launch configs when none exist:

```typescript
class DynLexDebugConfigProvider implements vscode.DebugConfigurationProvider {
    resolveDebugConfiguration(
        folder: vscode.WorkspaceFolder | undefined,
        config: vscode.DebugConfiguration
    ): vscode.ProviderResult<vscode.DebugConfiguration> {
        if (!config.program) {
            const editor = vscode.window.activeTextEditor;
            if (editor && editor.document.languageId === 'dynlex') {
                config.type = 'dynlex';
                config.request = 'launch';
                config.name = 'Debug DynLex';
                config.program = editor.document.uri.fsPath;
            }
        }
        return config;
    }
}
```

### 3.4 Bundling a Debugger Backend

To avoid requiring users to install GDB/LLDB:

**Option A (recommended): Detect and prompt.** Check for `gdb`/`lldb` on PATH. If missing, show a VS Code notification with an install command (`sudo apt install gdb`). Most Linux systems have GDB; macOS has LLDB via Xcode command line tools.

**Option B: Bundle GDB.** Ship a statically-linked `gdb` binary in the extension's `bin/` directory (~15MB compressed). Platform-specific: `bin/linux-x64/gdb`, `bin/darwin-arm64/lldb-mi`. The extension's `package.json` already has the pattern for resolving binaries (`bin/dynlex`).

**Option C: Use LLVM's liblldb.** Link liblldb into the `dynlex` binary itself, bypassing the need for an external debugger. This eliminates the MI protocol layer entirely but adds ~50MB to the binary and couples to a specific LLVM version (already pinned to LLVM 20).

Recommendation: Start with **Option A** (detect + prompt), implement **Option B** once the debugger works.

---

## File Inventory

### New Files
```
src/dap/
├── dapServer.h          # DAP server class
├── dapServer.cpp         # DAP request handlers, message loop
├── dapProtocol.h         # DAP type definitions (json schemas)
├── gdbmi.h               # GDB MI subprocess manager
└── gdbmi.cpp             # MI command sending, output parsing
```

### Modified Files
```
src/main.cpp                          # --dap flag, DAP server entry
src/compiler/parseContext.h           # DIBuilder fields, emitDebugInfo option
src/compiler/codegen/codegen.cpp      # DIBuilder init, debug locations, DISubprogram
src/compiler/codegen/codegenTypes.cpp # getDIType helper, variable debug info
src/compiler/codegen/codegenInternal.h # debug helper declarations
src/compiler/codegen/native.cpp       # -g flag on link command
CMakeLists.txt                        # add src/dap/*.cpp to build
vscode-extension/package.json         # debuggers + breakpoints contribution
vscode-extension/src/extension.ts     # debug adapter factory registration
```

---

## Implementation Order

1. **Phase 1.1–1.3**: `-g` flag, DIBuilder initialization — get a compilable skeleton
2. **Phase 1.5**: Debug locations on instructions — test with `llvm-dwarfdump` to verify DWARF output
3. **Phase 1.6–1.7**: DISubprogram for functions — verify with `gdb ./output -ex "info functions"`
4. **Phase 1.8**: Variable debug info — verify with `gdb ./output -ex "b main" -ex "run" -ex "info locals"`
5. **Phase 2.1–2.3**: DAP server skeleton with initialize/launch/disconnect
6. **Phase 2.4**: GDB MI wrapper — launch, breakpoints, continue, step
7. **Phase 2.5**: DynLex-aware name demangling
8. **Phase 3.1–3.2**: VS Code extension integration — end-to-end debugging works
9. **Phase 3.3–3.4**: Polish — auto-config, debugger bundling

Each phase is independently testable. Phase 1 can be validated with raw GDB before any DAP work begins.
