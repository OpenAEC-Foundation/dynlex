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

Requires C++23, CMake, Ninja, `nlohmann_json`, and LLVM 20+.

## Usage

```bash
./build/dynlex program.dl -o program.out && ./program.out
```

## Ubuntu PPA Packaging

Launchpad packaging lives in [`packaging/launchpad`](./packaging/launchpad). Use
that directory for Debian metadata, source-package builds, and PPA publishing so
the project root stays focused on the compiler itself.
