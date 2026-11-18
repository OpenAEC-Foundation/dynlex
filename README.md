# DynLex

A natural-language-like programming language that compiles to native code via LLVM.

UI libraries and the native/browser ribbon, Snake and text demos live in the separate
`dynlex-ui` repository. This compiler supplies their generic section and callback facilities.

## Features

- Pattern-based syntax that reads like English
- Compiles to native executables via LLVM
- Static typing with full type inference
- Standard library written in DynLex itself
- Named compile-time values via zero-argument replacement functions (see [`docs/named_compile_time_values.md`](./docs/named_compile_time_values.md))
- VS Code extension with LSP support

CAD and BIM geometry lives in the separate
[dynlex-cadkernel](https://github.com/OpenAEC-Foundation/dynlex-cadkernel)
repository.

## Build

```bash
./scripts/build.sh
```

Requires C++23, Clang 20 or newer, CMake, Ninja, `nlohmann_json`, Git, and GNU
binutils on ELF systems. `scripts/build.sh` checks out and caches the exact LLVM
23 fork recorded in `metadata/LLVM_TOOLCHAIN`; system LLVM installations are not
used. Linked Git worktrees automatically share the primary checkout's
`.cache/llvm-toolchain` directory. The matching LLD WebAssembly driver is built
and embedded in both compilers to finalize executable modules and their memory layout.
Set `DYNLEX_LLVM_CACHE_DIR` to use an explicit
cache location instead. Build that toolchain independently with:

```bash
./scripts/build_llvm.sh native
```

The normal build uses `-O2` while retaining debug information and compiler
invariants. Use `./scripts/build.sh --debug` when an unoptimized executable is
needed for source-level debugging.
Use `./scripts/build.sh -j1` to limit compilation to one job. `-j N`,
`--jobs N`, and `--jobs=N` also accept a positive job count.

## Tests

```bash
./scripts/test.sh
./scripts/test.sh --integration
```

The compiler build prepares the required C++ test executables before the
120-second process-tree budget starts. The budget covers the entire required
suite, including compiling and running DynLex fixtures and running the C++
and auxiliary checks. Required fixture builds and non-cache compiler checks
disable incremental reuse. Cache regressions start with fresh test projects.
For `./scripts/test.sh --run-only`, prepare the compiler and C++ test executables
with `./scripts/build.sh --test-tools`. The same process-tree budget applies.
The integration suite covers larger library and application programs, additional
compiler regressions, runtime behavior and tooling. It has individual check timeouts
but no overall 120-second limit. Release verification runs both suites.
The required gate checks cold and warm incremental builds, edit invalidation,
object recovery, managed global storage, and cache-entry integrity. The longer
incremental scenarios run in the integration suite. Linux, macOS, and Windows
CI also run those scenarios in a dedicated step with
`./scripts/test.sh --compiler-regressions --run-only`.
The CAD fixtures and their verifier live in dynlex-cadkernel.

Integration compilations have a 30-second default budget (60 seconds on Windows)
to include optimization of application libraries. Required fixtures retain their
10-second default (20 seconds on Windows). Fixture-specific compilation budgets
still apply through `compile_timeout_seconds.txt`.

## Browser Compiler

DynLex can be built as a browser-hosted compiler module (`dynlex_web.js/.wasm`).
The website and its editor are written in DynLex and rendered by the shared UI
library. Browser JavaScript supplies the graphics and compiler-worker adapters.
This target requires Emscripten (`emcmake`, `emcc`). The build compiles and
caches a WebAssembly version of the same pinned LLVM fork with the WebAssembly
and SPIR-V targets.
The build uses SPIRV-Cross for shader reflection and a pinned Naga WebAssembly
module to validate and translate emitted SPIR-V into WebGPU's WGSL.
See [stage interfaces](docs/shader_interfaces.md) for typed vertex/fragment values
and [resource bindings](docs/shader_resources.md) for textures, samplers and uniforms.

Build compiler WASM artifacts and copy them into the web app:

```bash
source ~/emsdk/emsdk_env.sh
./scripts/build_web.sh
```

The build script uses the active Emscripten SDK and builds the exact web LLVM
toolchain recorded in `metadata/LLVM_TOOLCHAIN`.

`./scripts/build_web.sh` refreshes `src/web/ide/public/compiler/dynlex_web.js` and
`src/web/ide/public/compiler/dynlex_web.wasm` and
`src/web/ide/public/compiler/dynlex_wgsl_translator.wasm`; commit those files
when updating the deployed web compiler.

Run the compiler WASM smoke test:

```bash
./scripts/test_web_smoke.sh
```

The website is maintained in [dynlex-website](https://github.com/OpenAEC-Foundation/dynlex-website).
The reusable UI framework is maintained in [dynlex-ui](https://github.com/OpenAEC-Foundation/dynlex-ui).
DynLex Studio and the embeddable code editor are maintained in [dynlex-ide](https://github.com/OpenAEC-Foundation/dynlex-ide).
This repository provides their compiler service and runtime distribution.

Export a relocatable compiler package after building the browser compiler:

```bash
python3 scripts/package_web_compiler.py --output build/dynlex-web.zip
```

The content-addressed package includes the browser compiler, standard library,
shader translator, generic compiler services, runtime, build tools, installer and
compiler branding. Consumers pin its manifest hash and build without a compiler
checkout. `scripts/compile_project.mjs` uses the browser compiler by default. Set
`DYNLEX_PROJECT_COMPILER` to an absolute installed compiler executable to compile
CPU jobs natively against the package standard library; shader jobs continue to
use the pinned browser compiler and translator. The installer is maintained at `scripts/install_release.sh`; the
canonical logo is `resources/branding/dynlex-icon.svg`.

Compiler WASM C ABI exports:
- `dynlex_web_init`
- `dynlex_web_set_main_source`
- `dynlex_web_compile_and_emit_wasm`
- `dynlex_web_compile_and_emit_shader_spirv`
- `dynlex_web_get_diagnostics_json`
- `dynlex_web_get_output_wasm_ptr` / `dynlex_web_get_output_wasm_len`
- `dynlex_web_get_output_wasm_base64`
- `dynlex_web_get_output_shader_spirv_base64`
- `dynlex_web_get_shader_reflection_json`
- `dynlex_web_get_compiler_log_json`
- `dynlex_web_lsp_exchange_json`

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
export PATH="$(brew --prefix rustup)/bin:$PATH"
export LIBRARY_PATH="$(brew --prefix glfw)/lib:$(brew --prefix freetype)/lib:$(brew --prefix vulkan-loader)/lib:$(brew --prefix)/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
```

The installer reuses tools already available on `PATH` and installs missing
Homebrew formulae without upgrading unrelated installed packages. The Homebrew
LLVM formula supplies the bootstrap Clang only; DynLex links the pinned fork
built by `scripts/build_llvm.sh`. Native graphics use the Vulkan loader on
Linux and Windows and the Vulkan loader with MoltenVK on macOS. The installer
also installs the pinned Rust 1.87.0 `wasm32-unknown-unknown` toolchain used to
build the browser's SPIR-V-to-WGSL translator.

Windows (winget):

```powershell
.\scripts\install.ps1
```

The Windows installer also installs `nlohmann_json`, GLFW, FreeType, and the
Vulkan loader through the pinned vcpkg manifest, then exports the package paths
used by `scripts/build.sh`.

## Usage

Compile a source file:

```bash
./build/dynlex program.dl -o program.out && ./program.out
```

Generated programs use `-O2` by default, including terminal source and WebAssembly
output. Select `-O0` for unoptimized code or `-O3` for additional optimization;
the last optimization flag wins. `-O2` does not enable fast math.
For source-level debugging, use `-g -O0`; `-g` alone retains the default `-O2`.

Native executable, object and LLVM text builds automatically reuse results from previous invocations.
To inspect the cache location and compilation work:

```bash
./build/dynlex program.dl --incremental-stats -o program.out
```

Unchanged source/configuration and compiler options reuse the complete compilation.
Comment edits can reuse analyzed code, and diagnostic-message edits in `config.dl` update
warnings and errors without recompiling. After code edits, unaffected parser and
matcher results can be restored. Without debug information or execution tracing,
eligible functions also reuse inference summaries and generated LLVM bodies at all
optimization levels. Optimized executable builds cache LLVM optimization by symbol groups,
including the callees imported into each group. Native
partitions reuse cached objects, and an unchanged executable skips linking.
Some stages still reconstruct shared state after code edits.
The cache checks file contents (including imports and syntax configuration), compiler
identity and target options. Corruption is reported as an error. Disk usage is limited
to 512 MiB in the system user cache directory under `dynlex/compiler`.
Use `--cache-dir <directory>` to choose a different location or `--no-incremental`
to perform a clean build. Deleting the cache is safe and makes the next build cold. Optional statistics
report frontend reuse and the numbers of reused/rebuilt objects on stderr.

Emit a native object for a definition-only library. `--no-main` omits the
entry point and rejects executable top-level statements; `exposed function`
declarations provide the callable C ABI. With no `-o`, object output is written
as `program.o`.

```bash
./build/dynlex library.dl --emit-object --no-main
```

`--no-main` can also be combined with `--emit-llvm` for definition-only LLVM
IR. Definition-only libraries cannot use command-line argument intrinsics,
because those values are initialized by DynLex's generated entry point.

Or execute DynLex source directly. Command-line source automatically imports
`lib/commands.dl`, which provides the standard library and command-oriented
libraries such as file operations:

```bash
./build/dynlex print 3
./build/dynlex 'move "a.txt" to "b.txt"'
```

The outer single quotes in the second example preserve DynLex's string quotes
when the source passes through the shell.

## Release

Compiler/release version source of truth is [`metadata/VERSION`](./metadata/VERSION).

Releases use `YEAR.WEEK.REVISION`, with ISO week numbers and a revision starting
at zero each week. Compiler, editor extension and WGSL translator share this
version. Release tags have a `v` prefix. The release clock uses UTC.

Create and push a release tag with the next version for the current week:

```bash
./scripts/release.sh         # or an explicit version: ./scripts/release.sh 2026.45.1
```

Windows MSI stores the year as an offset from 2000 (`26.45.1` for public version
`2026.45.1`) because its first version field is limited to 255. The compiler and
all source package metadata retain the full public version.

## ASan Leak Check (With Third-Party Suppressions)

For sanitizer leak checks that ignore known process-lifetime allocations from
third-party libraries (for example LLVM internals), use:

```bash
./scripts/asan_leak_check.sh
```

By default this runs `build-asan/dynlex` with `scripts/lsan.supp`.

## Performance Benchmarks

Benchmark details, reproducible source programs, compiler settings, and
hardware-specific results:

- [`tests/benchmarks/01_sum_100m.md`](./tests/benchmarks/01_sum_100m.md)
- [`tests/benchmarks/02_collatz.md`](./tests/benchmarks/02_collatz.md)

Times are hardware- and toolchain-dependent; run the benchmark files locally
for your exact environment.

## Ubuntu PPA Packaging

Launchpad packaging lives in [`packaging/launchpad`](./packaging/launchpad). Use
that directory for Debian metadata, source-package builds, and PPA publishing so
the project root stays focused on the compiler itself.
