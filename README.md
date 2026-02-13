# DynLex

A natural-language-like programming language that compiles to native code via LLVM.

## Features

- Pattern-based syntax that reads like English
- Compiles to native executables via LLVM
- Static typing with full type inference
- Standard library written in DynLex itself
- VS Code extension with LSP support

## Build

```bash
./scripts/build.sh
```

Requires C++23, Conan, and LLVM.

## Usage

```bash
./build/dynlex program.dl -o program.out && ./program.out
```
