#!/usr/bin/env node
import net from 'net';
import { spawn } from 'child_process';
import { fileURLToPath } from 'url';
import path from 'path';

// MCP server for LSP integration
// Provides token-efficient tools to query language servers

// Base LSP client
class LSPClient {
  constructor() {
    this.messageId = 1;
    this.pendingRequests = new Map();
    this.buffer = '';
    this.initialized = false;
  }

  handleData(data) {
    this.buffer += data.toString();

    while (true) {
      const headerEnd = this.buffer.indexOf('\r\n\r\n');
      if (headerEnd === -1) break;

      const headers = this.buffer.substring(0, headerEnd);
      const contentLengthMatch = headers.match(/Content-Length: (\d+)/i);

      if (!contentLengthMatch) break;

      const contentLength = parseInt(contentLengthMatch[1]);
      const messageStart = headerEnd + 4;
      const messageEnd = messageStart + contentLength;

      if (this.buffer.length < messageEnd) break;

      const messageContent = this.buffer.substring(messageStart, messageEnd);
      this.buffer = this.buffer.substring(messageEnd);

      try {
        const message = JSON.parse(messageContent);
        this.handleMessage(message);
      } catch (err) {
        console.error('Failed to parse message:', err);
      }
    }
  }

  handleMessage(message) {
    if (message.id && this.pendingRequests.has(message.id)) {
      const { resolve, reject } = this.pendingRequests.get(message.id);
      this.pendingRequests.delete(message.id);

      if (message.error) {
        reject(new Error(message.error.message));
      } else {
        resolve(message.result);
      }
    }
  }

  sendRequest(method, params) {
    return new Promise((resolve, reject) => {
      const id = this.messageId++;
      const message = {
        jsonrpc: '2.0',
        id,
        method,
        params
      };

      this.pendingRequests.set(id, { resolve, reject });

      const content = JSON.stringify(message);
      const header = `Content-Length: ${content.length}\r\n\r\n`;

      this.write(header + content);

      // Timeout after 10 seconds
      setTimeout(() => {
        if (this.pendingRequests.has(id)) {
          this.pendingRequests.delete(id);
          reject(new Error('Request timeout'));
        }
      }, 10000);
    });
  }

  async initialize(rootPath) {
    const result = await this.sendRequest('initialize', {
      processId: process.pid,
      rootUri: rootPath ? `file://${rootPath}` : null,
      capabilities: {
        textDocument: {
          hover: { contentFormat: ['plaintext', 'markdown'] },
          definition: { linkSupport: true },
          references: {},
          documentSymbol: {}
        }
      }
    });

    // Send initialized notification
    this.sendNotification('initialized', {});

    this.initialized = true;
    return result;
  }

  sendNotification(method, params) {
    const message = {
      jsonrpc: '2.0',
      method,
      params
    };
    const content = JSON.stringify(message);
    const header = `Content-Length: ${content.length}\r\n\r\n`;
    this.write(header + content);
  }

  write(data) {
    throw new Error('write() must be implemented by subclass');
  }

  disconnect() {
    throw new Error('disconnect() must be implemented by subclass');
  }
}

// TCP-based LSP client (for 3BX)
class TCPLSPClient extends LSPClient {
  constructor(host, port) {
    super();
    this.host = host;
    this.port = port;
    this.socket = null;
  }

  async connect() {
    return new Promise((resolve, reject) => {
      this.socket = new net.Socket();

      this.socket.on('data', (data) => this.handleData(data));
      this.socket.on('error', (err) => reject(err));
      this.socket.on('close', () => {
        this.initialized = false;
      });

      this.socket.connect(this.port, this.host, async () => {
        await this.initialize();
        resolve();
      });
    });
  }

  write(data) {
    this.socket.write(data);
  }

  disconnect() {
    if (this.socket) {
      this.socket.destroy();
      this.socket = null;
    }
  }
}

// Stdio-based LSP client (for clangd, etc.)
class StdioLSPClient extends LSPClient {
  constructor(command, args, rootPath) {
    super();
    this.command = command;
    this.args = args;
    this.rootPath = rootPath;
    this.process = null;
  }

  async connect() {
    return new Promise((resolve, reject) => {
      console.error(`Starting ${this.command} ${this.args.join(' ')}`);

      this.process = spawn(this.command, this.args, {
        stdio: ['pipe', 'pipe', 'pipe']
      });

      this.process.stdout.on('data', (data) => this.handleData(data));

      this.process.stderr.on('data', (data) => {
        // Log stderr but don't fail
        console.error(`LSP stderr: ${data.toString().trim()}`);
      });

      this.process.on('error', (err) => {
        reject(err);
      });

      this.process.on('exit', (code) => {
        this.initialized = false;
        console.error(`LSP process exited with code ${code}`);
      });

      // Wait a bit for the process to start, then initialize
      setTimeout(async () => {
        try {
          await this.initialize(this.rootPath);
          resolve();
        } catch (err) {
          reject(err);
        }
      }, 100);
    });
  }

  write(data) {
    if (this.process && this.process.stdin) {
      this.process.stdin.write(data);
    }
  }

  disconnect() {
    if (this.process) {
      this.process.kill();
      this.process = null;
    }
  }
}

// MCP implementation
class MCPServer {
  constructor() {
    this.lspClients = new Map();
    this.rootPath = process.env.PROJECT_ROOT || process.cwd();
  }

  // Determine which LSP server to use based on file extension
  getServerKey(uri) {
    const lowerUri = uri.toLowerCase();

    if (lowerUri.endsWith('.3bx')) {
      return '3bx';
    } else if (lowerUri.endsWith('.cpp') || lowerUri.endsWith('.hpp') ||
               lowerUri.endsWith('.cc') || lowerUri.endsWith('.h') ||
               lowerUri.endsWith('.cxx') || lowerUri.endsWith('.c')) {
      return 'cpp';
    }

    // Default to C++ for this project
    return 'cpp';
  }

  async getLSPClient(uri) {
    const serverKey = this.getServerKey(uri);

    if (!this.lspClients.has(serverKey)) {
      let client;

      if (serverKey === '3bx') {
        // TCP connection to 3BX LSP server
        client = new TCPLSPClient('127.0.0.1', 5007);
        await client.connect();
      } else if (serverKey === 'cpp') {
        // Stdio connection to clangd
        const clangdPath = process.env.CLANGD_PATH || 'clangd';
        client = new StdioLSPClient(clangdPath, [
          '--background-index',
          '--header-insertion=never',
          '--log=error'
        ], this.rootPath);
        await client.connect();
      }

      this.lspClients.set(serverKey, client);
      console.error(`Connected to ${serverKey} LSP server`);
    }

    return this.lspClients.get(serverKey);
  }

  // Ensure document is opened in LSP server
  async ensureDocumentOpen(client, uri) {
    // Simple heuristic: if we haven't seen this URI before, open it
    if (!client.openDocs) {
      client.openDocs = new Set();
    }

    if (!client.openDocs.has(uri)) {
      // Mark as opening immediately to prevent race conditions
      client.openDocs.add(uri);

      // Read file content
      const fs = await import('fs');
      const filePath = uri.replace('file://', '');
      let text = '';
      try {
        text = fs.readFileSync(filePath, 'utf8');
      } catch (err) {
        console.error(`Failed to read ${filePath}:`, err.message);
        return;
      }

      // Send didOpen notification
      client.sendNotification('textDocument/didOpen', {
        textDocument: {
          uri,
          languageId: this.getLanguageId(uri),
          version: 1,
          text
        }
      });
    }
  }

  getLanguageId(uri) {
    const lowerUri = uri.toLowerCase();
    if (lowerUri.endsWith('.3bx')) return '3bx';
    if (lowerUri.endsWith('.cpp') || lowerUri.endsWith('.cc') || lowerUri.endsWith('.cxx')) return 'cpp';
    if (lowerUri.endsWith('.h') || lowerUri.endsWith('.hpp')) return 'cpp';
    if (lowerUri.endsWith('.c')) return 'c';
    return 'plaintext';
  }

  // Convert file:// URI to relative path if inside project
  formatLocation(uri) {
    const filePath = uri.replace('file://', '');
    const rootPath = this.rootPath;

    if (filePath.startsWith(rootPath + '/')) {
      return filePath.slice(rootPath.length + 1);
    }
    return filePath;
  }

  // Normalize input URI - convert relative paths to file:// URIs
  normalizeUri(uri) {
    if (uri.startsWith('file://')) {
      return uri;
    }
    // If it's a relative path, make it absolute
    const absolutePath = path.isAbsolute(uri) ? uri : path.join(this.rootPath, uri);
    return `file://${absolutePath}`;
  }

  // Find character position of a word on a line
  async findWordPosition(uri, line, word) {
    const fs = await import('fs');
    const filePath = uri.replace('file://', '');
    try {
      const text = fs.readFileSync(filePath, 'utf8');
      const lines = text.split('\n');
      if (line >= lines.length) {
        throw new Error(`Line ${line} not found in file`);
      }
      const lineText = lines[line];
      const index = lineText.indexOf(word);
      if (index === -1) {
        throw new Error(`Word "${word}" not found on line ${line}`);
      }
      return index;
    } catch (err) {
      throw new Error(`Failed to find word: ${err.message}`);
    }
  }

  // Resolve LSP position: normalize URI, get client, and resolve character from word if needed
  async resolvePosition(params) {
    const uri = this.normalizeUri(params.uri);
    const client = await this.getLSPClient(uri);
    await this.ensureDocumentOpen(client, uri);

    let character = params.character;
    if (params.word !== undefined && params.line !== undefined) {
      character = await this.findWordPosition(uri, params.line, params.word);
    }

    return { uri, client, character };
  }

  // Tool: Get definition location (compact format)
  async definition(params) {
    const { uri, client, character } = await this.resolvePosition(params);

    const result = await client.sendRequest('textDocument/definition', {
      textDocument: { uri },
      position: { line: params.line, character }
    });

    if (!result) {
      return { found: false };
    }

    // Handle both single location and array of locations
    const location = Array.isArray(result) ? result[0] : result;
    if (!location) {
      return { found: false };
    }

    // Return compact format: path:line:char
    return {
      found: true,
      location: `${this.formatLocation(location.uri)}:${location.range.start.line}:${location.range.start.character}`
    };
  }

  // Tool: Get hover info (compact, just the content)
  async hover(params) {
    const { uri, client, character } = await this.resolvePosition(params);

    const result = await client.sendRequest('textDocument/hover', {
      textDocument: { uri },
      position: { line: params.line, character }
    });

    if (!result || !result.contents) {
      return { found: false };
    }

    // Extract just the text content
    let text = '';
    if (typeof result.contents === 'string') {
      text = result.contents;
    } else if (result.contents.value) {
      text = result.contents.value;
    } else if (Array.isArray(result.contents)) {
      text = result.contents.map(c => typeof c === 'string' ? c : c.value).join('\n');
    }

    return { found: true, text };
  }

  // Tool: Get references (compact list)
  async references(params) {
    const { uri, client, character } = await this.resolvePosition(params);

    const result = await client.sendRequest('textDocument/references', {
      textDocument: { uri },
      position: { line: params.line, character },
      context: { includeDeclaration: params.includeDeclaration ?? true }
    });

    if (!result || result.length === 0) {
      return { found: false, count: 0 };
    }

    // Return compact format
    return {
      found: true,
      count: result.length,
      locations: result.map(loc =>
        `${this.formatLocation(loc.uri)}:${loc.range.start.line}:${loc.range.start.character}`
      )
    };
  }

  // Tool: Get document symbols (compact tree)
  async symbols(params) {
    const { uri, client } = await this.resolvePosition(params);

    const result = await client.sendRequest('textDocument/documentSymbol', {
      textDocument: { uri }
    });

    if (!result || result.length === 0) {
      return { found: false };
    }

    // Flatten hierarchical symbols and return compact format
    const flattenSymbols = (syms, depth = 0) => {
      const result = [];
      for (const sym of syms) {
        result.push({
          name: sym.name,
          kind: sym.kind,
          line: sym.range?.start?.line ?? sym.location?.range?.start?.line ?? 0,
          depth
        });
        if (sym.children) {
          result.push(...flattenSymbols(sym.children, depth + 1));
        }
      }
      return result;
    };

    return { found: true, symbols: flattenSymbols(result) };
  }

  // Handle MCP protocol
  async handleRequest(request) {
    try {
      switch (request.method) {
        case 'initialize':
          return {
            protocolVersion: '2024-11-05',
            capabilities: {
              tools: {}
            },
            serverInfo: {
              name: 'lsp-mcp',
              version: '1.0.0'
            }
          };

        case 'tools/list':
          return {
            tools: [
              {
                name: 'lsp_def',
                description: 'Jump to definition. Returns file:line:char\n\nUsage:\n- Takes a file URI (absolute, relative path, or file://) and position of a symbol\n- Line and character are 0-indexed\n- Provide either `character` or `word` (symbol name to find on the line)\n- Use when you see a type/function/class reference and want to navigate to its definition\n\nExample: After reading a file and seeing PatternMatch on line 7, use {uri: "expression.h", line: 7, word: "PatternMatch"}',
                inputSchema: {
                  type: 'object',
                  properties: {
                    uri: { type: 'string', description: 'File URI, absolute path, or relative path' },
                    line: { type: 'number', description: 'Line number (0-indexed)' },
                    character: { type: 'number', description: 'Character offset (0-indexed). Not required if word is provided.' },
                    word: { type: 'string', description: 'Symbol name to find on the line. Alternative to character.' }
                  },
                  required: ['uri', 'line']
                }
              },
              {
                name: 'lsp_refs',
                description: 'Find all references. Returns compact locations list\n\nUsage:\n- Takes a file URI and position of a symbol\n- Line and character are 0-indexed\n- Provide either `character` or `word` (symbol name to find on the line)\n- Set includeDeclaration: false to exclude the definition itself\n- Use to find all usages of a function, class, variable, or type\n\nExample: Before refactoring a function, use this to see all call sites.',
                inputSchema: {
                  type: 'object',
                  properties: {
                    uri: { type: 'string', description: 'File URI, absolute path, or relative path' },
                    line: { type: 'number', description: 'Line number (0-indexed)' },
                    character: { type: 'number', description: 'Character offset (0-indexed). Not required if word is provided.' },
                    word: { type: 'string', description: 'Symbol name to find on the line. Alternative to character.' },
                    includeDeclaration: { type: 'boolean', default: true, description: 'Include the definition in results' }
                  },
                  required: ['uri', 'line']
                }
              },
              {
                name: 'lsp_hover',
                description: 'Get hover info (type/docs). Returns text only\n\nUsage:\n- Takes a file URI and position of a symbol\n- Line and character are 0-indexed\n- Provide either `character` or `word` (symbol name to find on the line)\n- Returns type signatures, function parameters, and documentation\n- Use for quick type/signature lookup without navigating away\n\nExample: Check a function\'s parameters before calling it.',
                inputSchema: {
                  type: 'object',
                  properties: {
                    uri: { type: 'string', description: 'File URI, absolute path, or relative path' },
                    line: { type: 'number', description: 'Line number (0-indexed)' },
                    character: { type: 'number', description: 'Character offset (0-indexed). Not required if word is provided.' },
                    word: { type: 'string', description: 'Symbol name to find on the line. Alternative to character.' }
                  },
                  required: ['uri', 'line']
                }
              },
              {
                name: 'lsp_symbols',
                description: 'List document symbols. Returns name@line\n\nUsage:\n- Takes a file URI\n- Returns all classes, functions, and symbols defined in the file\n- Use to get an overview of file structure before reading\n\nExample: See what functions exist in a file without reading the whole thing.',
                inputSchema: {
                  type: 'object',
                  properties: {
                    uri: { type: 'string', description: 'File URI, absolute path, or relative path' }
                  },
                  required: ['uri']
                }
              }
            ]
          };

        case 'tools/call':
          const toolName = request.params.name;
          const args = request.params.arguments;

          switch (toolName) {
            case 'lsp_def':
              return { content: [{ type: 'text', text: JSON.stringify(await this.definition(args)) }] };
            case 'lsp_refs':
              return { content: [{ type: 'text', text: JSON.stringify(await this.references(args)) }] };
            case 'lsp_hover':
              return { content: [{ type: 'text', text: JSON.stringify(await this.hover(args)) }] };
            case 'lsp_symbols':
              return { content: [{ type: 'text', text: JSON.stringify(await this.symbols(args)) }] };
            default:
              throw new Error(`Unknown tool: ${toolName}`);
          }

        default:
          throw new Error(`Unknown method: ${request.method}`);
      }
    } catch (error) {
      return { error: { code: -32603, message: error.message } };
    }
  }

  start() {
    // Read from stdin, write to stdout (MCP protocol)
    let buffer = '';

    process.stdin.on('data', async (chunk) => {
      buffer += chunk.toString();

      // Process line-delimited JSON
      const lines = buffer.split('\n');
      buffer = lines.pop(); // Keep incomplete line in buffer

      for (const line of lines) {
        if (line.trim()) {
          try {
            const request = JSON.parse(line);
            const response = await this.handleRequest(request);

            if (request.id !== undefined) {
              console.log(JSON.stringify({ jsonrpc: '2.0', id: request.id, result: response }));
            }
          } catch (err) {
            console.error('Error processing request:', err);
            if (request?.id !== undefined) {
              console.log(JSON.stringify({
                jsonrpc: '2.0',
                id: request.id,
                error: { code: -32603, message: err.message }
              }));
            }
          }
        }
      }
    });

    process.stdin.on('end', () => {
      // Cleanup
      for (const client of this.lspClients.values()) {
        client.disconnect();
      }
      process.exit(0);
    });
  }
}

// Start the server
const server = new MCPServer();
server.start();

console.error('LSP MCP Server started');
