# DynLex Language Support

Language support for the DynLex programming language in Visual Studio Code.

## Features

- **Syntax Highlighting**: Server-driven semantic highlighting for DynLex code
- **Diagnostics**: Real-time error and warning reporting as you type
- **Go to Definition**: Navigate to variable and pattern definitions (F12 or Ctrl+Click)
- **Incremental Sync**: Efficient document synchronization on every keystroke

## Requirements

- The `dynlex` compiler must be installed and accessible
- By default, the extension prefers a workspace `build/dynlex`, then a bundled binary, then `dynlex` on `PATH`

## Extension Settings

This extension contributes the following settings:

- `dynlex.server.port`: Port for the DynLex language server (default: 5007)
- `dynlex.server.path`: Custom path to the dynlex executable (leave empty to use the default resolution)
- `dynlex.server.flags`: Additional flags passed to the DynLex language server process
- `dynlex.server.useExternal`: Connect to an already-running DynLex language server instead of spawning one

## Commands

- **DynLex: Restart Language Server**: Restart the language server if it becomes unresponsive

## Token Types

The extension provides semantic highlighting for the following token types:

- `function` - Function patterns
- `section` - Section patterns
- `variable` - Variables (with `definition` modifier for definitions)
- `comment` - Comments
- `patternDefinition` - Pattern definitions (with `definition` modifier)
- `number` - Numeric literals
- `string` - String literals
- `intrinsic` - Built-in intrinsic functions
- `type` - Type references
- `keyword` - Language keywords

## Troubleshooting

If the language server fails to start:

1. Check the Output panel (View > Output) and select "DynLex Language Server"
2. Verify that `dynlex` is built and accessible at the configured path
3. Ensure the configured DynLex server port is available when using TCP mode

## Building from Source

```bash
cd vscode-extension
npm install
npm run compile
```

To test the extension, press F5 in VS Code to launch an Extension Development Host.
