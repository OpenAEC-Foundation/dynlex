# Agent Instructions

These instructions apply to all coding agents working in this repository (including Codex and Claude).

## Core Rules
- this is a compiler. only PERFECT code is accepted. if the existing code isn't perfect, we need to find the root cause and fix it.
- your code should be as DRY and performant as possible. implement things FULLY, remove ALL leftovers. we don't have 'legacy'. ALL main .dl files should compile within seconds.
- be direct. don't hide anything relevant.
- remember, you're an agent. for you, a ' ' is a token, just like an 'e'.
- find the broader, deterministic and simple pattern.
- don't add code for debugging. debugging information gathering should NEVER hurt performance. handy optional built-in tools which can help debugging are allowed, but only on the users request.
- Keep failure handling explicit and transparent by reporting real errors instead of masking them.
- Fail hard on internal stage inconsistencies instead of repairing invalid intermediate state.
- Minimize complexity. Deliver clean, complete solutions.
- Generalize reusable logic instead of duplicating one-off fixes.
- Drive ALL language behavior through patterns and intrinsics. so NO HARDCODING.
- Keep dependencies minimal.
- Always answer direct user questions instead of doing. if the user didn't ask you to do something, propose it.
- if you need information about the code you can get yourself, just get it. you don't have to ask the user if they want you to root-cause a problem. gathering info can never hurt, since you aren't making changes.
- don't assume the user knows everything.
 - when the user asks an explanation, give the context as well. the user might have read past it.
 - suggest better approaches when you see one.
 - immediately point out any inconsistencies in what the user is saying. this helps making things clear.
- Verify before assuming package/version/tool availability.
- Document important fixes in repository docs/rules so all agents share the same context.
- Be cautious with git; other agents may work in the same tree. if the user tells you to revert that doesn't mean git revert or git checkout. verify no other agents edited a file before deleting or editing it.
- Inspect intermediates instead of binaries.
- Understand full scope before fixing; gather context first.
- Use appropriate tools (for example, prefer `std::stack` over `std::vector` for stack-like structures).
- Prefer MCP/LSP-aware refactoring tools over blind search-replace when available.
- Keep bash commands on one line by chaining with `&&` or `;`.
- When encountering ANY compiler issue:

NEVER add a workaround like:
- internal code validation
- narrow case 'fixes'
- temporary solutions
- a fallback
- using bad or slow alternatives because 'the best one isn't implemented yet'

instead:
1. identify a minimal reproducible example. minimize the amount of reproducing code. imported code is counted too. so NO 'import std.dl'!
2. identify the root cause with whatever tools you need. stay open for any root cause. to find the root cause, keep asking yourself 'but why ...' until you find the wrong code.
3. identify a possible fix. use gdb for this preferrably, to avoid flooding your context and the code with debug statements and such.
4. report to the user.
the user may discuss the bug with you.
when the user tells you to fix it:
5. fix the compiler bug first. verify it fixed it by building and the running repro and test script. if it didn't fix it and you don't know why, go back to 1. when you are at step 3 again, you don't have to report if it's a trivial fix following agents.md.
if the compiler bug was found using buggy .dl code:
6. fix the .dl bug after.

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

- `src/cpp/main.cpp`: Entry point (`--lsp`, `--dap`)
- `src/cpp/compiler/`: Core compiler (import, section analysis, pattern resolution, type inference, codegen)
- `src/cpp/compiler/codegen/`: LLVM codegen, intrinsics, SPIR-V/native backends
- `src/cpp/dap/`: Debug Adapter Protocol server (GDB MI backend)
- `src/cpp/lsp/`: Language server
- `src/web/ide/`: Browser IDE source (Vite + Monaco)
- `web/`: Static web root (site/wiki source + deployed assets)
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
- Tests may include `expected.txt` (output match) or `expected_diagnostics.txt` (exact diagnostics match).

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
