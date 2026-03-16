# Agent Instructions

These instructions apply to all coding agents working in this repository (including Codex and Claude).

## Core Rules

- If an implementation gets stuck, stop and report the exact blocker to the user.
- Keep failure handling explicit and transparent by reporting real errors instead of masking them.
- Fail hard on internal stage inconsistencies instead of repairing invalid intermediate state.
- Minimize complexity. Deliver clean, complete solutions.
- Fix the root cause instead of adding a fallback.
- Generalize reusable logic instead of duplicating one-off fixes.
- Drive ALL language behavior through patterns and intrinsics. so NO HARDCODING.
- Keep dependencies minimal.
- Always answer direct user questions instead of doing. if the user didn't ask you to do something, propose it.
- Suggest better approaches when you see one.
- Verify before assuming package/version/tool availability.
- Document important fixes in repository docs/rules so all agents share the same context.
- Be cautious with git; other agents may work in the same tree. if the user tells you to revert that doesn't mean git revert or git checkout. verify no other agents edited a file before deleting or editing it.
- Inspect intermediates instead of binaries.
- Understand full scope before fixing; gather context first.
- Use appropriate tools (for example, prefer `std::stack` over `std::vector` for stack-like structures).
- Prefer MCP/LSP-aware refactoring tools over blind search-replace when available.
- Keep bash commands on one line by chaining with `&&` or `;`.
- When encountering ANY compiler issue while working on .dl code:
do NOT add a workaround. temporary solutions are NOT accepted.
1. identify the root cause with whatever tools you need.
2. identify a minimal reproducible example and possible fix.
3. report to the user.
- this is a compiler. only PERFECT code is accepted.
- your code should be as DRY and performant as possible. implement things FULLY, remove ALL leftovers. we don't have 'legacy'. ALL main .dl files should compile within seconds.
- be direct. don't hide anything relevant.
- remember, you're an agent. for you, a ' ' is a token, just like an 'e'.

## Project Overview

DynLex is a natural-language-like programming language designed for humans and AI agents.

Current priority: compiler maturity (fix remaining test failures, improve errors, expand stdlib).

## Build & Run

```bash
./scripts/build.sh
./build/dynlex <file.dl>
./build/dynlex --lsp
./build/dynlex --dap
```

Dependencies: C++23, Conan (`nlohmann_json`), LLVM 20.

## Project Structure

- `src/main.cpp`: Entry point (`--lsp`, `--dap`)
- `src/compiler/`: Core compiler (import, section analysis, pattern resolution, type inference, codegen)
- `src/compiler/codegen/`: LLVM codegen, intrinsics, SPIR-V/native backends
- `src/dap/`: Debug Adapter Protocol server (GDB MI backend)
- `src/lsp/`: Language server
- `vscode-extension/`: VS Code extension (TypeScript)
- `tests/required/`: Required test cases

Implementation details are documented in `.claude/rules/` (`compiler.md`, `codegen.md`, `lsp.md`, `dap.md`).

## Language Basics

- File extension: `.dl`
- Pattern types:
  - `function`: A function pattern (void-returning for side effects)
  - `section`: Outermost pattern for section openings (loop/if/etc.)
- Classes add patterns to the function tree because type literals are functions.
- Intrinsics should stay minimal (core arithmetic/memory/comparison only); stdlib should live in DynLex.

## Compilation Pipeline

1. Import (read sources, handle imports)
2. Section analysis (indentation, sections, patterns)
3. Pattern resolution (matching and variable resolution)
4. Codegen (LLVM IR to native executable, `.ll`, or `.spv`)

## Testing

```bash
./scripts/test.sh
./build/dynlex tests/required/simple/main.dl -o tests/required/simple/main.out && ./tests/required/simple/main.out
```

- Compiled test binaries use `.out` extension.
- Tests may include `expected.txt` (output match) or `expected_error.txt` (compile-fail substring match).

## Key Design Decisions

- Debugging: DWARF (`-g`) + DAP server (`--dap`) through GDB.
- Compilation target: native code via LLVM (`.ll`, `.spv`, or executable by flags).
- Type system: static typing with full inference.
- DynLex language memory: automatic scope-based destruction (RAII style).
- Compiler-internal memory: arena-style lifetime owned by parse context for compilation duration.
- Primitive types: `i8/i16/i32/i64`, `f32/f64`, `bool`, `string`.
- Classes: data-only structs (no member functions).
- Pattern ambiguity: compile error when multiple patterns match.

## Current State

- Core libs include `lib/std.dl`, `lib/graphics.dl`, `lib/font.dl`, `lib/string.dl`, `lib/random.dl`, `lib/vector.dl`.
- Shader gallery works with animated SPIR-V shaders.
- Typed pattern arguments and overload dispatch are in use.
- Snake game is used as a practical compiler validation target.

## Future Plans

- Potential multi-threading: parallelize independent compilation phases.
