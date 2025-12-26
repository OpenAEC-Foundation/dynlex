# 3BX Language Support for VS Code

This extension provides comprehensive language support for the 3BX programming language in Visual Studio Code.

## Features

### Syntax Highlighting

Full syntax highlighting for 3BX source files (`.3bx`):

- Reserved words (`set`, `to`, `if`, `then`, `else`, `while`, `loop`, `function`, `return`, etc.)
- Pattern definitions (`pattern:`, `syntax:`, `when triggered:`, `when parsed:`)
- Intrinsic calls (`@intrinsic(...)`)
- Strings, numbers, and comments
- Section headers

### Language Configuration

- Comment toggling with `#`
- Bracket matching for `()`, `[]`, `{}`
- Auto-closing pairs for brackets and strings
- Indentation-based code folding

### Code Snippets

Quickly insert common 3BX constructs:

| Prefix | Description |
|--------|-------------|
| `pattern` | Pattern definition with syntax and trigger |
| `patternp` | Pattern with priority |
| `patternc` | Pattern with compile-time behavior |
| `set` | Variable assignment |
| `if` | If-then statement |
| `ifelse` | If-then-else statement |
| `while` | While loop |
| `function` | Function definition |
| `import` | Import statement |
| `@print` | Print intrinsic |
| `@store` | Store intrinsic |
| `@load` | Load intrinsic |
| `@add`, `@sub`, `@mul`, `@div` | Arithmetic intrinsics |

### Language Server Protocol (LSP) Integration

When the 3BX compiler supports the `--lsp` flag, the extension provides:

- Real-time error diagnostics
- Code completion
- Hover information
- Go-to-definition

**Note:** LSP features require the 3BX compiler with `--lsp` support. Syntax highlighting works without the compiler.

## Installation

### From VS Code Marketplace

Search for "3BX" in the Extensions view (`Ctrl+Shift+X`) and click Install.

### From VSIX File

1. Build the extension: `npm run package`
2. Install: `code --install-extension 3bx-0.1.0.vsix`

### Development Setup

```bash
cd vscode-extension
npm install
npm run compile
```

Then press `F5` in VS Code to launch the Extension Development Host.

## Configuration

Configure the extension in VS Code settings:

| Setting | Default | Description |
|---------|---------|-------------|
| `3bx.compiler.path` | `""` | Path to the 3BX compiler. If empty, uses `threebx` from PATH. |
| `3bx.lsp.enabled` | `true` | Enable Language Server Protocol features |
| `3bx.lsp.trace.server` | `off` | Trace communication with the language server (`off`, `messages`, `verbose`) |

## Commands

| Command | Description |
|---------|-------------|
| `3BX: Restart Language Server` | Restart the language server |
| `3BX: Compile Current File` | Compile the currently open 3BX file |

## Example 3BX Code

```3bx
# Define a greeting pattern
pattern:
    syntax: greet name
    when triggered:
        @intrinsic("print", name)

# Use the pattern
greet "World"

# Variable assignment
set x to 42
set y to x + 10
```

## Requirements

- VS Code 1.75.0 or higher
- 3BX compiler (optional, for LSP features)

## Known Issues

- LSP features require compiler support (not yet implemented in the compiler)
- Pattern-based completion suggestions are pending compiler integration

## Release Notes

### 0.1.0

- Initial release
- Syntax highlighting for 3BX files
- Language configuration (comments, brackets, folding)
- Code snippets for common patterns
- LSP client infrastructure (ready for compiler integration)

## Contributing

Contributions are welcome! Please see the main 3BX repository for contribution guidelines.

## License

MIT
