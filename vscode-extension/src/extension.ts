import * as vscode from 'vscode';
import * as net from 'net';
import * as path from 'path';
import * as fs from 'fs';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    StreamInfo,
} from 'vscode-languageclient/node';
import { buildManagedServerOptions } from './managedServer';

let client: LanguageClient | undefined;
let outputChannel: vscode.LogOutputChannel;
let isShuttingDown = false;
let extensionPath: string;
let cursorClientId = '';
let lastSentCursorKey: string | undefined;
let instantiationHoverProvider: DynLexInstantiationHoverProvider | undefined;

interface DynLexInstantiationOption {
    key: string;
    label: string;
}

interface DynLexInstantiationLensEntry {
    selectionKey: string;
    currentKey: string;
    range: vscode.Range;
    options: DynLexInstantiationOption[];
}

function isRecord(value: unknown): value is Record<string, unknown> {
    return typeof value === 'object' && value !== null;
}

function isPosition(value: unknown): value is { line: number; character: number } {
    return isRecord(value) && typeof value.line === 'number' && typeof value.character === 'number';
}

class DynLexInstantiationHoverProvider implements vscode.HoverProvider {
    async provideHover(document: vscode.TextDocument, position: vscode.Position): Promise<vscode.Hover | undefined> {
        if (!client || document.languageId !== 'dynlex') {
            return undefined;
        }

        try {
            const response = await client.sendRequest('dynlex/instantiationsInDocument', {
                uri: document.uri.toString()
            }) as unknown;
            if (!Array.isArray(response)) {
                return undefined;
            }

            const entries: DynLexInstantiationLensEntry[] = [];
            for (const raw of response) {
                if (!isRecord(raw) || !isRecord(raw.range)) {
                    continue;
                }
                const start = raw.range.start;
                const end = raw.range.end;
                if (!isPosition(start) || !isPosition(end)) {
                    continue;
                }
                const optionsRaw = raw.options;
                if (!Array.isArray(optionsRaw) || optionsRaw.length === 0) {
                    continue;
                }
                const options: DynLexInstantiationOption[] = [];
                for (const option of optionsRaw) {
                    if (isRecord(option) && typeof option.key === 'string' && typeof option.label === 'string') {
                        options.push({ key: option.key, label: option.label });
                    }
                }
                entries.push({
                    selectionKey: typeof raw.selectionKey === 'string' ? raw.selectionKey : '',
                    currentKey: typeof raw.currentKey === 'string' ? raw.currentKey : '',
                    range: new vscode.Range(
                        new vscode.Position(start.line, start.character),
                        new vscode.Position(end.line, end.character),
                    ),
                    options,
                });
            }

            const entry = entries.find(item => item.range.contains(position));
            if (!entry || !entry.selectionKey) {
                return undefined;
            }

            const markdown = new vscode.MarkdownString('', true);
            markdown.isTrusted = { enabledCommands: ['dynlex.selectInstantiationPath'] };
            markdown.appendMarkdown('pick an instance:\n\n');
            for (const option of entry.options) {
                const selectedPrefix = option.key === entry.currentKey ? 'current: ' : '';
                const commandArgs = encodeURIComponent(JSON.stringify([entry.selectionKey, option.key]));
                markdown.appendMarkdown(`[${selectedPrefix}${option.label}](command:dynlex.selectInstantiationPath?${commandArgs})  \n`);
            }

            return new vscode.Hover(markdown, entry.range);
        } catch (err) {
            logError(`Failed to fetch DynLex hover paths: ${err}`);
            return undefined;
        }
    }
}

export function activate(context: vscode.ExtensionContext) {
    extensionPath = context.extensionPath;
    cursorClientId = vscode.env.sessionId || `dynlex-${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;
    outputChannel = vscode.window.createOutputChannel('DynLex Language Server', { log: true });
    context.subscriptions.push(outputChannel);

    log('DynLex extension activating...');
    log(`Extension path: ${extensionPath}`);

    // Start the language server
    void startLanguageServer(context);

    // Watch for file changes and notify the server
    const fileWatcher = vscode.workspace.createFileSystemWatcher('**/*.dl');
    fileWatcher.onDidChange(uri => {
        log(`File changed on disk: ${uri.fsPath}`);
        // The LSP client handles didSave notifications automatically
    });
    fileWatcher.onDidCreate(uri => {
        log(`File created: ${uri.fsPath}`);
    });
    fileWatcher.onDidDelete(uri => {
        log(`File deleted: ${uri.fsPath}`);
    });
    context.subscriptions.push(fileWatcher);

    // Register debug configuration provider (enables F5 without launch.json)
    context.subscriptions.push(
        vscode.debug.registerDebugConfigurationProvider('dynlex', {
            resolveDebugConfiguration(
                _folder: vscode.WorkspaceFolder | undefined,
                config: vscode.DebugConfiguration,
            ): vscode.ProviderResult<vscode.DebugConfiguration> {
                if (!config.type && !config.request && !config.name) {
                    const editor = vscode.window.activeTextEditor;
                    if (editor && editor.document.languageId === 'dynlex') {
                        config.type = 'dynlex';
                        config.name = 'Debug DynLex Program';
                        config.request = 'launch';
                        config.program = '${file}';
                        config.cwd = '${workspaceFolder}';
                    }
                }
                if (!config.program) {
                    vscode.window.showErrorMessage('Cannot find a DynLex file to debug');
                    return undefined;
                }
                return config;
            }
        })
    );

    // Register debug adapter
    context.subscriptions.push(
        vscode.debug.registerDebugAdapterDescriptorFactory('dynlex', {
            createDebugAdapterDescriptor() {
                const serverPath = getServerPath();
                const cwd = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
                return new vscode.DebugAdapterExecutable(serverPath, ['--dap'], { cwd });
            }
        })
    );

    // Register restart command
    context.subscriptions.push(
        vscode.commands.registerCommand('dynlex.restartServer', () => {
            log('Restarting language server...');
            void stopLanguageServer().then(() => startLanguageServer(context));
        })
    );

    instantiationHoverProvider = new DynLexInstantiationHoverProvider();
    context.subscriptions.push(
        vscode.languages.registerHoverProvider({ language: 'dynlex', scheme: 'file' }, instantiationHoverProvider)
    );
    context.subscriptions.push(
        vscode.commands.registerCommand('dynlex.selectInstantiationPath', async (selectionKey: string, instantiationKey: string) => {
            if (!client || !selectionKey || !instantiationKey) {
                return;
            }
            await client.sendNotification('dynlex/selectInstantiation', {
                selectionKey,
                instantiationKey,
            });
            await vscode.commands.executeCommand('editor.action.showHover');
        })
    );

    context.subscriptions.push(
        vscode.window.onDidChangeActiveTextEditor((editor) => {
            void sendActiveCursorNotification(editor);
        })
    );
    context.subscriptions.push(
        vscode.window.onDidChangeTextEditorSelection((event) => {
            if (event.textEditor === vscode.window.activeTextEditor) {
                void sendActiveCursorNotification(event.textEditor);
            }
        })
    );
    context.subscriptions.push(
        vscode.workspace.onDidChangeTextDocument((event) => {
            const editor = vscode.window.activeTextEditor;
            if (editor && event.document === editor.document) {
                void sendActiveCursorNotification(editor);
            }
        })
    );
}

export function deactivate(): Thenable<void> | undefined {
    isShuttingDown = true;
    return stopLanguageServer();
}

function log(message: string) {
    const timestamp = new Date().toISOString();
    outputChannel.appendLine(`[${timestamp}] ${message}`);
    console.log(`[DynLex] ${message}`);
}

function logError(message: string) {
    const timestamp = new Date().toISOString();
    outputChannel.appendLine(`[${timestamp}] ERROR: ${message}`);
    console.error(`[DynLex ERROR] ${message}`);
}

function getServerPath(): string {
    const config = vscode.workspace.getConfiguration('dynlex');
    const configuredPath = config.get<string>('server.path');

    if (configuredPath && configuredPath.length > 0) {
        if (!path.isAbsolute(configuredPath) && vscode.workspace.workspaceFolders?.[0]) {
            return path.join(vscode.workspace.workspaceFolders[0].uri.fsPath, configuredPath);
        }
        return configuredPath;
    }

    // Check relative to extension installation path
    // In development: extension is at {project}/vscode-extension, binary at {project}/build/dynlex
    // In production: extension is installed, binary should be bundled or in PATH
    const devPath = path.join(extensionPath, '..', 'build', 'dynlex');
    if (fs.existsSync(devPath)) {
        return devPath;
    }

    // Check for bundled binary (production)
    const bundledPath = path.join(extensionPath, 'bin', 'dynlex');
    if (fs.existsSync(bundledPath)) {
        return bundledPath;
    }

    // Fall back to assuming it's in PATH
    return 'dynlex';
}

function getServerPort(): number {
    const config = vscode.workspace.getConfiguration('dynlex');
    return config.get<number>('server.port') || 5007;
}

function getServerHost(): string {
    const config = vscode.workspace.getConfiguration('dynlex');
    const host = (config.get<string>('server.host') || '').trim();
    return host.length > 0 ? host : '127.0.0.1';
}

function getServerHosts(): string[] {
    const configured = getServerHost();
    const hosts = [configured];
    // For local development/debugging, try both loopback families in case one is unavailable.
    if (configured === '127.0.0.1' || configured === '::1' || configured === 'localhost') {
        hosts.push('127.0.0.1', '::1', 'localhost');
    }
    return Array.from(new Set(hosts));
}

function getServerFlags(): string[] {
    const config = vscode.workspace.getConfiguration('dynlex');
    const flags = config.get<string>('server.flags') || '';
    return flags.split(/\s+/).filter(f => f.length > 0);
}

function useExternalServer(): boolean {
    const config = vscode.workspace.getConfiguration('dynlex');
    return config.get<boolean>('server.useExternal') || false;
}

async function waitForPort(port: number, hosts: string[], timeoutMs: number = 30000): Promise<string | undefined> {
    const startTime = Date.now();
    let attempt = 0;
    while (Date.now() - startTime < timeoutMs) {
        if (isShuttingDown) {
            return undefined;
        }
        attempt++;
        for (const host of hosts) {
            const connected = await new Promise<boolean>(resolve => {
                const socket = new net.Socket();
                socket.setTimeout(500);
                socket.on('connect', () => {
                    socket.destroy();
                    resolve(true);
                });
                socket.on('error', () => {
                    socket.destroy();
                    resolve(false);
                });
                socket.on('timeout', () => {
                    socket.destroy();
                    resolve(false);
                });
                socket.connect(port, host);
            });
            if (connected) {
                return host;
            }
        }
        log(`Connection attempt ${attempt} failed, retrying...`);
        await new Promise(resolve => setTimeout(resolve, 200));
    }
    return undefined;
}

async function startLanguageServer(context: vscode.ExtensionContext) {
    if (client || isShuttingDown) {
        return;
    }

    let serverOptions: ServerOptions;

    if (useExternalServer()) {
        const port = getServerPort();
        const hosts = getServerHosts();
        log(`Waiting for external server on ${hosts.join(', ')}:${port}...`);
        const readyHost = await waitForPort(port, hosts);
        if (!readyHost) {
            logError(`Timed out waiting for external server on ${hosts.join(', ')}:${port}`);
            vscode.window.showErrorMessage(`Timed out waiting for DynLex language server on ${hosts.join(', ')}:${port}`);
            return;
        }
        log(`External server is ready`);
        serverOptions = createExternalServerOptions(port, readyHost);
    } else {
        const serverPath = getServerPath();
        const extraFlags = getServerFlags();
        const cwd = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;

        log(`Server path resolved to: ${serverPath}`);
        log(`Starting managed language server over stdio: ${serverPath} --stdio ${extraFlags.join(' ')}`.trim());
        serverOptions = buildManagedServerOptions(serverPath, extraFlags, cwd);
    }

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'dynlex' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.dl')
        },
        outputChannel: outputChannel
    };

    const startingClient = new LanguageClient(
        'dynlex-language-server',
        'DynLex Language Server',
        serverOptions,
        clientOptions
    );
    client = startingClient;

    try {
        await startingClient.start();
        log('Language client started successfully');
        context.subscriptions.push(startingClient);
        lastSentCursorKey = undefined;
        await sendActiveCursorNotification(vscode.window.activeTextEditor);
    } catch (err) {
        if (client === startingClient) {
            client = undefined;
        }
        logError(`Failed to start language client: ${err}`);
        vscode.window.showErrorMessage(`Failed to start DynLex language server: ${err}`);
    }
}

function createExternalServerOptions(port: number, host: string): ServerOptions {
    return (): Promise<StreamInfo> => {
        log(`Connecting to external language server on ${host}:${port}...`);
        return new Promise((resolve, reject) => {
            const socket = new net.Socket();
            socket.setTimeout(2000);

            socket.on('connect', () => {
                // Disable idle timeout after connect; this socket stays open for the full LSP session.
                socket.setTimeout(0);
                log(`Connected to language server at ${host}:${port}`);
                resolve({
                    reader: socket,
                    writer: socket
                });
            });

            socket.on('error', (err) => {
                reject(new Error(`Unable to connect to ${host}:${port} (${err.message})`));
            });

            socket.on('timeout', () => {
                socket.destroy();
                reject(new Error(`Unable to connect to ${host}:${port} (timeout)`));
            });

            socket.on('close', () => {
                log('Socket closed');
            });

            socket.connect(port, host);
        });
    };
}

async function sendActiveCursorNotification(editor: vscode.TextEditor | undefined) {
    if (!client) {
        return;
    }

    const document = editor?.document;
    if (!editor || !document || document.languageId !== 'dynlex') {
        const key = `${cursorClientId}:none`;
        if (lastSentCursorKey === key) {
            return;
        }
        lastSentCursorKey = key;
        await client.sendNotification('dynlex/activeCursorChanged', { clientId: cursorClientId });
        return;
    }

    const position = editor.selection.active;
    const payload = {
        clientId: cursorClientId,
        uri: document.uri.toString(),
        version: document.version,
        position: {
            line: position.line,
            character: position.character,
        }
    };
    const key = `${cursorClientId}:${payload.uri}:${payload.version}:${payload.position.line}:${payload.position.character}`;
    if (lastSentCursorKey === key) {
        return;
    }
    lastSentCursorKey = key;
    await client.sendNotification('dynlex/activeCursorChanged', payload);
}

async function stopLanguageServer(): Promise<void> {
    if (client) {
        const stoppingClient = client;
        client = undefined;
        try {
            await stoppingClient.stop();
            log('Language client stopped');
        } catch (err) {
            logError(`Error stopping client: ${err}`);
        }
    }
}
