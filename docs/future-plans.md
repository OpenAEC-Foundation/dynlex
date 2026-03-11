# Future Plans

Features and design decisions planned for later implementation.

## Concurrency

- Async/await model for asynchronous programming

## Error Handling

- Plan: Exceptions that exit sections until a block with a catch intrinsic is reached
- All internal workings handled via intrinsics

## Module System

- File = module
- Everything public by default
- `local` modifier for private definitions
- Import system (partially implemented, some test cases still failing)

## Platform Targets

- Cross-platform (Linux, macOS, Windows)
- Future: JavaScript compilation for browser/universal support

## AI Integration

- Language model integration planned
- Output tokens filtered by pattern tree
- Ensures AI can only output valid syntax

## Decompilation to DynLex

- Decompile any executable into readable DynLex code
- Pipeline: binary → LLVM IR (via lifters like RetDec/Remill) → DynLex pattern matching → AI naming pass
- AI generates meaningful function/variable names by analyzing behavior, string literals, system calls, and data flow patterns
- DynLex's natural-language syntax makes decompiled output genuinely readable, unlike traditional C decompilation
- Could leverage debug symbols (DWARF) when available for even better results

# Matching names in debugging, better printing and function evaluation
# It function (refer to subject)
# 