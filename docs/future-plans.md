# Future Plans

Features and design decisions planned for later implementation.

## Concurrency

- Async/await model for asynchronous programming

## Error Handling

- Not implemented yet
- Plan: Exceptions that exit sections until a block with a catch intrinsic is reached
- All internal workings handled via intrinsics

## Module System

- File = module
- Everything public by default
- `local` modifier for private definitions

## Platform Targets

- Cross-platform (Linux, macOS, Windows)
- Future: JavaScript compilation for browser/universal support

## Multi-Language Syntax

- Nothing hardcoded in the compiler
- Any human language can be used for patterns
- Example: Dutch syntax works (see `tests/required/6_languagetest/`)

## AI Integration

- Language model integration planned
- Output tokens filtered by pattern tree
- Ensures AI can only output valid syntax

## Project History

- Name origin: 3BM (company name) + X (executable)
- Now under Impertio Studio
