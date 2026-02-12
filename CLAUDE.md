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
```

**Dependencies:** C++23, Conan (nlohmann_json), LLVM (for codegen - to be added)

**Implementation Details:** See `.claude/rules/implementation.md` for detailed documentation on type inference, bugs fixed, and implementation details.

## Current Priority

**LLVM code generation**

## Project Structure

```
src/
├── main.cpp              # Entry point (--lsp flag for LSP mode)
├── compiler/             # Core compiler
│   ├── compiler.cpp/h    # Main compilation pipeline
│   ├── section/          # Section types (expression, effect, custom)
│   ├── pattern/          # Pattern definitions and references
│   └── pattern/pattern_tree/  # Tree-based pattern matching
├── lsp/                  # Language server (port 5007)
└── pexlit/               # C++ utility library (git submodule)
vscode-extension/         # VS Code extension (TypeScript)
tests/required/           # Test cases with expected outputs
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
4. **(TODO) Codegen** - Generate LLVM IR → executable or .ll file

## Code Conventions

- **Minimize complexity** - Solutions must be clean and complete, no temporary workarounds
- **Generalize** - Extract reusable components (e.g., languageServer is generic, dynlexServer uses it)
- **No hardcoding** - Nothing language-specific hardcoded; syntax comes from patterns
- **Minimal dependencies** - Only LLVM for codegen, avoid other external deps
- **Suggest improvements** - If you know a better approach, mention it

## Testing

Test files in `tests/required/`. Each folder has a `.dl` file and expected output.

Run test: Build compiler → compile test to `.out` → execute → compare with expected.

```bash
./build/dynlex tests/required/0_simple/main.dl -o tests/required/0_simple/main.out && ./tests/required/0_simple/main.out
```

Compiled test binaries use the `.out` extension (gitignored).

**Current state:** `0_simple` patterns parse correctly. Other tests may need work.

## Key Design Decisions

- **Compilation target:** Native code via LLVM (outputs .ll or executable based on flags)
- **Type system:** Static typing with full inference (no annotations)
- **Memory (DynLex language):** Automatic scope-based destruction (RAII-style)
- **Memory (Compiler internals):** Arena-style allocation - objects allocated with `new` during compilation are not explicitly deleted. They're owned by ParseContext and cleaned up when compilation finishes. This includes: CodeLine, Section, Expression, Variable, PatternDefinition, PatternReference, VariableReference, MatchProgress. No smart pointers needed.
- **Primitive types:** Sized numerics (i8/i16/i32/i64, f32/f64), bool, string
- **Classes:** Data-only structs (no member functions), patterns operate on them
- **Pattern ambiguity:** Compiler error if multiple patterns match
