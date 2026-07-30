# DynLex Language Support

Language support for the DynLex programming language in Visual Studio Code,
VSCodium, and other compatible editors.

## Features

- **Syntax Highlighting**: Server-driven semantic highlighting for DynLex code
- **Diagnostics**: Real-time error and warning reporting as you type
- **Go to Definition**: Navigate to variable and pattern definitions (F12 or Ctrl+Click)
- **Incremental Sync**: Efficient document synchronization on every keystroke

## Requirements

- Visual Studio Code 1.91 or newer, or a compatible editor
- The `dynlex` compiler must be installed and accessible
- By default, the extension prefers a workspace `build/dynlex`, then a bundled binary, then `dynlex` on `PATH`

Install DynLex from [dynlex.com](https://dynlex.com/) before enabling the
extension.

## Extension Settings

This extension contributes the following settings:

- `dynlex.server.host`: Host for an externally managed TCP language server
- `dynlex.server.port`: Port for an externally managed TCP language server (default: 5007)
- `dynlex.server.path`: Custom path to the dynlex executable (leave empty to use the default resolution)
- `dynlex.server.flags`: Additional flags passed to the managed DynLex language server process
- `dynlex.server.useExternal`: Connect to an already-running DynLex language server instead of spawning one

Managed language servers use stdio and are isolated per VS Code window. Host and port settings apply only when
`dynlex.server.useExternal` is enabled.

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
3. When using external mode, ensure the configured DynLex server is listening on the configured host and port

## Building from Source

```bash
cd vscode-extension
npm ci
npm run lint
npm test
```

To test the extension, press F5 in VS Code to launch an Extension Development Host.

## Extension Debug Workspaces (Repo)

- `.vscode/lsp-managed.code-workspace`: managed mode (`dynlex.server.useExternal=false`). Use this for normal extension development.
- `.vscode/lsp-debug.code-workspace`: external mode (`dynlex.server.useExternal=true`, `localhost:5008`). Use this when the LSP server is started separately (for C++ debugging).
- `.vscode/launch.json` contains explicit extension host launch configs for both modes.
- `Extension + C++ Debugger` uses a compound-level prep task (`prepare external lsp debug`) and no-prep child debug configs so build runs once and failures stop the whole compound launch.
