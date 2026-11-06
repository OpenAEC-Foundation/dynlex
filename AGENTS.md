# Agent Instructions

You are a professional development agent. You follow these rules:

## Core Rules
- Drive ALL language behavior through patterns and intrinsics. So NO HARDCODING.
- Keep failure handling explicit and transparent by reporting real errors instead of masking them.
- Fail hard on internal stage inconsistencies instead of repairing invalid intermediate state.
- Keep dependencies minimal.
- Always answer direct user questions instead of doing. If the user didn't ask you to do something, propose it.
- If you need information about the code you can get yourself, just get it. You don't have to ask the user if they want you to root-cause a problem. Gathering info can never hurt, since you aren't making changes.
- Be direct. Don't hide anything relevant.
- Don't assume the user knows everything.
- Be cautious with git; other agents may work in the same tree. If the user tells you to revert that doesn't mean `git revert` or `git checkout`. Verify no other agents edited a file before deleting or editing it.
- Inspect intermediates instead of binaries.
- When debugging the DynLex C++ compiler with GDB, ALWAYS use `./scripts/debug.sh ...` instead of plain `gdb` so the pretty-printer loads.
- Understand full scope before fixing; gather context first.
- Use appropriate tools (for example, prefer `std::stack` over `std::vector` for stack-like structures).
- Prefer MCP/LSP-aware refactoring tools over blind search-replace when available.
- If working on the compiler, read `docs/stages.md`. Follow `stages.md` and `agents.md`.

- When you receive ANY message, follow these steps:
  1. Gather context.

  2. Verify everything:
     - What the user said. Allow yourself the time to think. Maybe their assumptions are wrong. Maybe there are multiple ways to interpret a message.
     - Suggest better approaches when you see one.

     If you found any of these:
     - Point it out directly and stop. This will save you tons and tons of work.

  3. If the user asked a question, answer the user and stop.
     - When the user asks an explanation, give the context as well. The user might have read past it.
     - When the user gave you an instruction first and then broke in to ask a question, ask if you can proceed with the instruction if the instruction is still relevant.

  4. Plan your approach.

  5. Execute the users prompt.

  6. Verify your changes.
     Update or add any tests BEFORE testing, to prevent updating them as workaround.
     If you can verify anything likely to break textually, you are free to create a test for it.
     Make sure the testing code isn't actually part of the actual code.
     For example, when creating a car game in DynLex, make a test where the car spawns in and the forward button is pressed by the code and measure the distance driven. In this case, make sure the code is modular and import the relevant `.dl` files from another testing `.dl` file.

     When working on the compiler:
     - Run the test suite.
     - Use any appropriate tool in the scripts.

     For example, we have a fuzzer, a LSP token inspector, et cetera. If you verified there is no suitable testing tool yet but you have an idea for a handy tool, report that to the user.

     You are done when:
     - When working on the compiler, the tests pass.
     - When working on anything else, its tests pass.

     If there are regressions, root cause those using the steps below.

  7. Stop and summarize your changes concisely.

- When encountering ANY compiler issue during ANY of the previous steps:
  1. Identify a minimal reproducible example. Minimize the amount of reproducing code. Imported code is counted too. So NO `import std.dl`!
  2. Identify the root cause with whatever tools you need.
     Use `./scripts/debug.sh ...` for GDB-based compiler debugging, to avoid flooding your context and the code with debug print statements and such.
     Stay open for any root cause.
     To find the root cause, keep asking yourself 'but why ...' until you find the wrong code.
  3. Identify a possible fix.
  4. Report to the user and stop so the user is notified. The user is not reading what you are doing continuously.
     The user may discuss the bug with you.

     When the user tells you to fix it:
     5. Fix the compiler bug first. Verify it fixed it by building and the running repro and test script. If it didn't fix it and you don't know why, go back to 1. When you are at step 3 again, you don't have to report if it's a trivial fix following `agents.md`.
     6. If the compiler bug was found using buggy `.dl` code:
        Fix that DynLex code bug.
     7. Continue with your original task.

NEVER add a workaround like:

- Internal code validation (if `expr == nullptr`: ignore; clone this tree because the original tree keeps corrupting)
- Narrow case 'fixes' (when the pattern starts with an `'a'`: we do something different)
- Temporary solutions
- A fallback
- Using bad or slow alternatives because 'the best one isn't implemented yet'
- Changing the tests instead of fixing the root cause

All of these have in common that it FEELS like it helps, but it won't long term. Since we're coding at such a basic level (the compiler itself), you have the power to trace down and fix the root cause.

And NEVER revert prompt-following changes without the users permission. Don't be scared of failing tests. If you followed the users prompt but the tests are failing and you reported them, you have done well.

## Project Overview

DynLex is a natural-language-like programming language designed for humans and AI agents.

Current priority: compiler maturity (fix remaining test failures, improve errors, expand stdlib).

## Worktrees

Create repository-local worktrees through the project script:

```bash
./scripts/create-worktree.sh <name> [start-point]
```

The script creates `.worktrees/<name>` and a same-named branch from `start-point` (or `HEAD`). If the branch already exists, it checks out that branch instead. Worktree names must be single path components. Run subsequent commands from the created worktree. Do not create project worktrees manually in `/tmp` or elsewhere.

## Build & Run

```bash
./scripts/build.sh
./build/dynlex <file.dl>
./build/dynlex --lsp
./build/dynlex --dap
```

The normal build is optimized with debug information and invariants enabled.
Use `./scripts/build.sh --debug` only when an unoptimized compiler is required
for debugger stepping.

Dependencies: C++23, `nlohmann_json` (system package on Unix, vcpkg on Windows),
Clang 20+ as the bootstrap compiler, and the pinned LLVM 23 fork built by
`scripts/build_llvm.sh`.

## Project Structure

- `src/cpp/main.cpp`: Entry point (`--lsp`, `--dap`)
- `src/cpp/compiler/`: Core compiler (import, section analysis, pattern resolution, type inference, codegen)
- `src/cpp/compiler/codegen/`: LLVM codegen, intrinsics, SPIR-V/native backends
- `src/cpp/dap/`: Debug Adapter Protocol server (GDB MI backend)
- `src/cpp/lsp/`: Language server
- `src/web/ide/`: Browser IDE source (Vite + Monaco)
- `web/`: Static web root (site/wiki source + deployed assets)
- `vscode-extension/`: VS Code extension (TypeScript)
- `tests/required/`: Focused compiler test cases; the suite has a two-minute budget after the compiler build.
- `tests/integration/`: Library and application test cases. CAD geometry and its fixtures live in [dynlex-cadkernel](https://github.com/OpenAEC-Foundation/dynlex-cadkernel).

Implementation details are documented in `docs/stages.md`.

## Language Basics

- File extension: `.dl`
- Pattern types:
  - `function`: A function pattern (producing `nothing` when it only has side effects)
  - `section`: Outermost pattern for section openings (loop/if/etc.)
- Classes add patterns to the function tree because type literals are functions.
- Intrinsics should stay minimal (core arithmetic/memory/comparison only); stdlib should live in DynLex.

## Testing

```bash
./scripts/test.sh
./scripts/test.sh --integration
./build/dynlex tests/required/simple/main.dl -o tests/required/simple/main.out && ./tests/required/simple/main.out
```
- The test script will build as well. so either run the build or the test script.
- Required fixtures use no application-library imports. Use local helper files for import behavior tests.
- Run the integration suite when changing a library or compiler behavior exercised by those fixtures.
- Compiled test binaries use `.out` extension.
- Tests may include `expected.txt` (output match) or `expected_diagnostics.txt` (exact diagnostics match after source line numbers are removed). Diagnostic expectations must use `path.dl:column-range` locations; including a source line number makes the test invalid.

## Key Design Decisions

- Debugging: DWARF (`-g`) + DAP server (`--dap`) through GDB.
- Compilation target: native code via LLVM (`.ll`, `.spv`, or executable by flags).
- Type system: static typing with full inference.
- DynLex language memory: automatic scope-based destruction (RAII style).
- Compiler-internal memory: arena-style lifetime owned by parse context for compilation duration.
- Primitive types: byte and bit-width integers, bit-width floating-point numbers, booleans, and strings.
- Classes: data-only structs (no member functions).
- Pattern ambiguity: compile error when multiple patterns match.

## Current State

- Core libs include `lib/std.dl`, `lib/graphics.dl`, `lib/font.dl`, `lib/string.dl`, `lib/random.dl`, `lib/vector.dl`.
- Shader gallery works with animated SPIR-V shaders.
- Typed pattern arguments and overload dispatch are in use.
- Snake game is used as a practical compiler validation target.

## Future Plans

- Potential multi-threading: parallelize independent compilation phases.
