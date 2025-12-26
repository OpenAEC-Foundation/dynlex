# 3BX Compiler - AI Agent Guidelines

## Project Overview

3BX is a programming language designed to be readable by both humans and AI. It uses Skript-like natural language syntax with user-definable patterns and compiles to native machine code via LLVM.

## Communication Style

- **Always ask questions using dialogues** (AskUserQuestion tool with multiple questions batched together)
- Keep responses concise and actionable
- Document decisions and install commands in README.md

## Language Design Principles

- **Syntax**: Skript-like, natural language style
- **Paradigms**: Multi-paradigm (imperative, functional, object-oriented)
- **Patterns**: User-definable syntax patterns for extensibility
- **Target**: General-purpose programming

## Agent Roles

Specialized agents work on different compiler components:

### 1. Lexer Agent
**Responsibility**: Tokenization of 3BX source code
**Files**: `src/lexer/`, `include/lexer/`
**Focus**:
- Token definitions and types
- Pattern matching for Skript-like syntax
- Handling user-defined patterns
- Error reporting with line/column info

### 2. Parser Agent
**Responsibility**: Syntax analysis and grammar
**Files**: `src/parser/`, `include/parser/`
**Focus**:
- Recursive descent or Pratt parsing
- Grammar rules for natural language syntax
- Precedence handling
- Syntax error recovery

### 3. AST Agent
**Responsibility**: Abstract Syntax Tree design
**Files**: `src/ast/`, `include/ast/`
**Focus**:
- Node types for all language constructs
- Visitor pattern implementation
- AST pretty-printing for debugging
- Source location tracking

### 4. Semantic Agent
**Responsibility**: Type checking and validation
**Files**: `src/semantic/`, `include/semantic/`
**Focus**:
- Type inference and checking
- Symbol table management
- Scope resolution
- Semantic error messages

### 5. Codegen Agent
**Responsibility**: LLVM IR generation
**Files**: `src/codegen/`, `include/codegen/`
**Focus**:
- LLVM IR emission
- Optimization passes
- Native code generation
- Runtime support

### 6. Test Agent
**Responsibility**: Test suite development
**Files**: `tests/`
**Focus**:
- Unit tests for each component
- Integration tests
- Example programs validation
- Regression testing

### 7. VS Code Extension Agent
**Responsibility**: Visual Studio Code language support for 3BX
**Files**: `vscode-extension/`
**Prompt**: `agents/vscode-extension.md`
**Focus**:
- Syntax highlighting (TextMate grammar for `.3bx` files)
- Language configuration (comments, brackets, auto-closing)
- Snippet definitions for common patterns
- Language Server Protocol (LSP) integration
- Code completion for reserved words and intrinsics
- Hover information for patterns and intrinsics
- Go-to-definition for user-defined patterns
- Error diagnostics integration with compiler
- Extension packaging and marketplace publishing

### 8. Coder Agent
**Responsibility**: Hands-on implementation across all compiler components
**Files**: All `src/` and `include/` directories
**Prompt**: `agents/coder.md`
**Focus**:
- Implementing new language features end-to-end
- Fixing bugs and resolving compiler issues
- Writing clean, maintainable C++17 code
- Testing changes with `./run_test.sh`
- Coordinating cross-component changes

## Coding Standards

- C++17 standard
- Use `snake_case` for functions and variables
- Use `PascalCase` for classes and types
- Header files in `include/`, implementation in `src/`
- Every public function needs a brief comment
- Use LLVM coding conventions where applicable

## Build Commands

```bash
# Configure
mkdir -p build && cd build && cmake ..

# Build
make -j$(nproc)

# Run tests
ctest

# Clean
make clean
```

## File Extensions

- `.3bx` - 3BX source files
- `.cpp` - C++ implementation
- `.hpp` - C++ headers

## Pattern System (Implemented)

The 3BX pattern system allows defining new syntax in 3BX itself:

```
pattern:
    syntax: greet name
    when triggered:
        @intrinsic("print", name)
```

### Pattern Syntax
- `syntax:` - Natural language template. Reserved words become literals, others become parameters.
- `when triggered:` - Runtime behavior using intrinsics
- `when parsed:` - Compile-time behavior (optional)

**Note:** Patterns are matched by specificity (more literal words = more specific). If two patterns conflict, the compiler will emit an error.

### Intrinsics
Intrinsics bridge patterns to LLVM operations:
- `@intrinsic("store", var, val)` - Store value in variable
- `@intrinsic("load", var)` - Load value from variable
- `@intrinsic("add", a, b)` - Addition
- `@intrinsic("sub", a, b)` - Subtraction
- `@intrinsic("mul", a, b)` - Multiplication
- `@intrinsic("div", a, b)` - Division
- `@intrinsic("print", val)` - Print to console

### Reserved Words (Literal in Patterns)
`set`, `to`, `if`, `then`, `else`, `while`, `loop`, `function`, `return`,
`is`, `the`, `a`, `an`, `and`, `or`, `not`, `pattern`, `syntax`, `when`,
`parsed`, `triggered`, `priority`, `import`

## Implementation Status

**Working:**
- Basic statements: `set x to 42`, arithmetic expressions
- Pattern definitions parsed and registered
- @intrinsic calls parsed and executed
- LLVM IR generation
- Main function wrapper for top-level code

**In Progress:**
- Pattern-based statement matching
- Prelude loading
- Import system

**Not Yet Implemented:**
- Proper indentation-based blocks
- Control flow (if/else, while)
- Functions
- Type inference for patterns
- Exhaustive pattern matching
