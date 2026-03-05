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

## Install Dependencies

Linux (apt/dnf/pacman/zypper):

```bash
./scripts/install.sh
```

macOS (Homebrew):

```bash
./scripts/install.sh
export PATH="$(brew --prefix llvm)/bin:$PATH"
```

Windows (Chocolatey, run in elevated PowerShell):

```powershell
.\scripts\install.ps1
$env:PATH="$env:ProgramFiles\LLVM\bin;$env:PATH"
$env:LLVM_DIR="$env:ProgramFiles\LLVM\lib\cmake\llvm"
```

## Usage

```bash
./build/dynlex program.dl -o program.out && ./program.out
```

## Performance Snapshot (vs Python)

From repository benchmarks:

| Benchmark | Python | DynLex O0 | DynLex O3 |
|----------|--------|-----------|-----------|
| Sum 0..100,000,000 | 6.556s | 0.196s (33x faster) | 0.001s (6556x faster) |
| Collatz 1..1,000,000 | 11.363s | 0.969s (12x faster) | 0.221s (51x faster) |

Benchmark details and source programs:
- [`tests/benchmarks/01_sum_100m.md`](./tests/benchmarks/01_sum_100m.md)
- [`tests/benchmarks/02_collatz.md`](./tests/benchmarks/02_collatz.md)

Times are hardware- and toolchain-dependent; run the benchmark files locally for your exact environment.

## Ubuntu PPA Packaging

Launchpad packaging lives in [`packaging/launchpad`](./packaging/launchpad). Use
that directory for Debian metadata, source-package builds, and PPA publishing so
the project root stays focused on the compiler itself.
