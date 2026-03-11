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
if brew info llvm@20 >/dev/null 2>&1; then
  export PATH="$(brew --prefix llvm@20)/bin:$PATH"
else
  export PATH="$(brew --prefix llvm)/bin:$PATH"
fi
```

Windows (winget):

```powershell
.\scripts\install.ps1
# if clang is not on PATH after install, set LLVM path manually:
$env:PATH="$env:ProgramFiles\LLVM\bin;$env:PATH"
$env:LLVM_DIR="$env:ProgramFiles\LLVM\lib\cmake\llvm"
```

## Usage

```bash
./build/dynlex program.dl -o program.out && ./program.out
```

## Release

Compiler/release version source of truth is [`metadata/VERSION`](./metadata/VERSION).

Create and push a release tag with automatic version bump:

```bash
./scripts/release.sh patch   # or: minor, major, set X.Y.Z
```

## ASan Leak Check (With Third-Party Suppressions)

For sanitizer leak checks that ignore known process-lifetime allocations from
third-party libraries (for example LLVM internals), use:

```bash
./scripts/run_asan_leak_check.sh
```

By default this runs `build-asan/dynlex` with `scripts/lsan.supp`.

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
