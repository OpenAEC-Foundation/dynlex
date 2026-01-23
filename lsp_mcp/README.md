# lsp-mcp

MCP server providing token-efficient access to Language Server Protocol (LSP) servers.

## Features

- **Token-efficient**: Compact output format (`file:line:char` vs full JSON)
- **Multi-language**: C++ (clangd) + 3BX support
- **Auto-detection**: Chooses correct LSP server based on file extension
- **Simple tools**: `lsp_def`, `lsp_refs`, `lsp_hover`, `lsp_symbols`

## Supported Languages

| Language | LSP Server | Connection | Extensions |
|----------|-----------|------------|------------|
| C++ | clangd | stdio | .cpp, .hpp, .cc, .h, .c |
| 3BX | ./build/3bx --lsp | TCP:5007 | .3bx |

## Tools

### `lsp_def` - Jump to Definition
```json
Input:  { "uri": "file:///path/file.cpp", "line": 10, "character": 5 }
Output: { "found": true, "location": "file:///path/other.h:42:8" }
```

### `lsp_refs` - Find References
```json
Input:  { "uri": "file:///...", "line": 10, "character": 5 }
Output: { "found": true, "count": 3, "locations": ["file:///a.cpp:10:5", ...] }
```

### `lsp_hover` - Get Type Info
```json
Input:  { "uri": "file:///...", "line": 10, "character": 5 }
Output: { "found": true, "text": "Type: int\nReturns sum" }
```

### `lsp_symbols` - List Document Symbols
```json
Input:  { "uri": "file:///..." }
Output: { "found": true, "symbols": [{"name": "foo", "kind": 12, "line": 5}] }
```

## Setup

1. **Install dependencies:**
   ```bash
   cd lsp_mcp
   npm install
   ```

2. **Install clangd** (if not already installed):
   ```bash
   # Ubuntu/Debian
   sudo apt install clangd

   # macOS
   brew install llvm
   ```

3. **Configure Claude Code** (add to `~/.config/claude-code/config.json` or `.claude/config.json`):
   ```json
   {
     "mcpServers": {
       "lsp": {
         "command": "node",
         "args": ["/absolute/path/to/3BX/lsp_mcp/index.js"],
         "env": {
           "PROJECT_ROOT": "/absolute/path/to/3BX"
         }
       }
     }
   }
   ```

4. **Start 3BX LSP server** (for .3bx files):
   ```bash
   ./build/3bx --lsp
   ```

## Environment Variables

- `PROJECT_ROOT`: Project root directory (default: cwd)
- `CLANGD_PATH`: Path to clangd binary (default: `clangd` in PATH)

## Token Efficiency

| Operation | Traditional (Grep+Read) | LSP MCP | Savings |
|-----------|------------------------|---------|---------|
| Find definition | ~1000 tokens | ~50 tokens | 20x |
| Find references | ~2000 tokens | ~100 tokens | 20x |
| Get type info | ~800 tokens | ~40 tokens | 20x |

## How It Works

1. File extension determines LSP server (.cpp → clangd, .3bx → 3BX)
2. Connects to server (stdio or TCP)
3. Sends LSP requests and returns compact results
4. Caches connections for performance
