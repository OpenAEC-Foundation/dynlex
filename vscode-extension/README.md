# 3BX Language Support

Language support for the 3BX programming language in Visual Studio Code.

## Features

- **Syntax Highlighting**: Server-driven semantic highlighting for 3BX code
- **Diagnostics**: Real-time error and warning reporting as you type
- **Go to Definition**: Navigate to variable and pattern definitions (F12 or Ctrl+Click)
- **Incremental Sync**: Efficient document synchronization on every keystroke

## Requirements

- The `3bx` compiler must be installed and accessible
- By default, the extension looks for `3bx` in your workspace's `build/` directory

## Extension Settings

This extension contributes the following settings:

- `3bx.server.port`: Port for the 3BX language server (default: 5007)
- `3bx.server.path`: Custom path to the 3bx executable (leave empty to use default)

## Commands

- **3BX: Restart Language Server**: Restart the language server if it becomes unresponsive

## Token Types

The extension provides semantic highlighting for the following token types:

- `expression` - Expression patterns
- `effect` - Effect patterns
- `section` - Section patterns
- `variable` - Variables (with `definition` modifier for definitions)
- `comment` - Comments
- `patternDefinition` - Pattern definitions (with `definition` modifier)

## Troubleshooting

If the language server fails to start:

1. Check the Output panel (View > Output) and select "3BX Language Server"
2. Verify that `3bx` is built and accessible at the configured path
3. Ensure port 5007 is not in use by another application

## Building from Source

```bash
cd vscode-extension
npm install
npm run compile
```

To test the extension, press F5 in VS Code to launch an Extension Development Host.
