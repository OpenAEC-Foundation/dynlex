# DynLex Compiler

A natural-language-like programming language designed for humans and AI agents.

**Important:** Keep this file updated. When plans change, features are added, or design decisions are made that future agents need to know, update this document.

## Build & Run

```bash
# Install dependencies and build
./scripts/build.sh

# Run compiler on a file
./build/dynlex <file.dl>

# Run LSP server (for VS Code extension)
./build/dynlex --lsp

# Run DAP debug adapter (for VS Code debugging, uses stdio)
./build/dynlex --dap
```

**Dependencies:** C++23, Conan (nlohmann_json), LLVM 20 (for codegen + SPIR-V backend)

**Implementation Details:** See `.claude/rules/implementation.md` for detailed documentation on type inference, bugs fixed, and implementation details.

## Current Priority

**Compiler maturity** — fix remaining test failures, improve error messages, expand standard library

## Project Structure

```
src/
├── main.cpp                    # Entry point (--lsp, --dap flags)
├── compiler/                   # Core compiler
│   ├── compiler.cpp/h          # Import, section analysis, intrinsic classifiers
│   ├── patternResolution.cpp   # Pattern matching and resolution
│   ├── typeInference.cpp       # Type inference (fixed-point iteration)
│   ├── section/                # Section types (expression, effect, custom)
│   ├── pattern/                # Pattern definitions and references
│   ├── pattern/pattern_tree/   # Tree-based pattern matching
│   └── codegen/                # LLVM code generation
│       ├── codegen.cpp/h       # Expression codegen, specialized functions, driver
│       ├── codegenInternal.h   # Shared declarations across codegen files
│       ├── codegenTypes.cpp    # Type utilities, macro infrastructure, variables
│       ├── codegenIntrinsics.cpp # Intrinsic code generation
│       ├── spirv.cpp/h         # SPIR-V shader backend
│       └── native.cpp/h        # Native executable backend
├── dap/                        # Debug Adapter Protocol server (GDB MI backend)
│   ├── dapServer.cpp/h         # DAP message loop, request handlers
│   ├── dapProtocol.h           # DAP JSON types (Source, Breakpoint, StackFrame, etc.)
│   └── gdbmi.cpp/h             # GDB MI subprocess manager and output parser
├── lsp/                        # Language server (port 5007, multi-file diagnostic tracking)
└── pexlit/                     # C++ utility library (git submodule)
vscode-extension/               # VS Code extension (TypeScript)
tests/required/                 # Test cases with expected outputs
```

## Language Basics

**File extension:** `.dl`

**Pattern types:**
- `effect` - Side effects (statements): `effect print msg:`
- `expression` - Return values: `expression left + right:`

**Example:**
```
macro effect set var to val:
    replacement:
        @intrinsic("store", var, val)

set x to 42
print x
```

**Intrinsics:** Keep minimal. Only basic ops (arithmetic, memory, comparison). Standard library will be written in DynLex itself.

## Compilation Pipeline

1. **Import** - Read source files, handle imports
2. **Section Analysis** - Parse indentation, identify sections, track patterns
3. **Pattern Resolution** - Match patterns, resolve variables
4. **Codegen** - Generate LLVM IR → native executable, .ll, or .spv

## Code Conventions

- **Minimize complexity** - Solutions must be clean and complete, no temporary workarounds
- **Generalize** - Extract reusable components (e.g., languageServer is generic, dynlexServer uses it)
- **No hardcoding** - Nothing language-specific hardcoded; syntax comes from patterns
- **Minimal dependencies** - Only LLVM for codegen, avoid other external deps
- **Suggest improvements** - If you know a better approach, mention it
- **Verify before assuming** - Always check what packages/versions are actually available (e.g., `apt-cache search`, `llc --version`, `apt-cache show`) before choosing a dependency version. Don't guess that a specific version exists or has a feature — verify it first.

## Testing

Test files in `tests/required/`. Each folder has a `.dl` file and expected output.

```bash
# Run all tests
./scripts/run_tests.sh

# Run a single test
./build/dynlex tests/required/simple/main.dl -o tests/required/simple/main.out && ./tests/required/simple/main.out
```

Compiled test binaries use the `.out` extension (gitignored).

Tests can have `expected.txt` (output comparison) or `expected_error.txt` (expected compilation failure, substring match).

**Current state:** 11 pass (simple, custom_patterns, language, loops, class, specificity, precedence, recursion, pattern, stale_trie, duplicate_pattern). 4 fail: globals (output format), import/lib/section (missing import files).

## Key Design Decisions

- **Debugging:** DWARF debug info (`-g` flag) + DAP server (`--dap`) for VS Code debugging via GDB
- **Compilation target:** Native code via LLVM (outputs .ll, .spv, or executable based on flags)
- **Type system:** Static typing with full inference (no annotations)
- **Memory (DynLex language):** Automatic scope-based destruction (RAII-style)
- **Memory (Compiler internals):** Arena-style allocation - objects allocated with `new` during compilation are not explicitly deleted. They're owned by ParseContext and cleaned up when compilation finishes. This includes: CodeLine, Section, Expression, Variable, PatternDefinition, PatternReference, VariableReference, MatchProgress. No smart pointers needed.
- **Primitive types:** Sized numerics (i8/i16/i32/i64, f32/f64), bool, string
- **Classes:** Data-only structs (no member functions), patterns operate on them
- **Pattern ambiguity:** Compiler error if multiple patterns match
