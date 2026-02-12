import * as vscode from 'vscode';
import * as net from 'net';
import * as path from 'path';
import * as fs from 'fs';
import { spawn, ChildProcess } from 'child_process';
import {
    LanguageClient,
    LanguageClientOptions,
    StreamInfo,
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;
let serverProcess: ChildProcess | undefined;
let outputChannel: vscode.OutputChannel;
let reconnectAttempts = 0;
let reconnectTimeout: NodeJS.Timeout | undefined;
let isShuttingDown = false;
let extensionPath: string;

const BASE_RECONNECT_DELAY = 5000; // 5 seconds
const MAX_RECONNECT_DELAY = 60000; // 1 minute

export function activate(context: vscode.ExtensionContext) {
    extensionPath = context.extensionPath;
    outputChannel = vscode.window.createOutputChannel('DynLex Language Server');
    context.subscriptions.push(outputChannel);

    log('DynLex extension activating...');
    log(`Extension path: ${extensionPath}`);

    // Start the language server
    startLanguageServer(context);

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

    // Register restart command
    context.subscriptions.push(
        vscode.commands.registerCommand('dynlex.restartServer', () => {
            log('Restarting language server...');
            reconnectAttempts = 0;
            stopLanguageServer().then(() => startLanguageServer(context));
        })
    );
}

export function deactivate(): Thenable<void> | undefined {
    isShuttingDown = true;
    if (reconnectTimeout) {
        clearTimeout(reconnectTimeout);
    }
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

function getServerFlags(): string[] {
    const config = vscode.workspace.getConfiguration('dynlex');
    const flags = config.get<string>('server.flags') || '';
    return flags.split(/\s+/).filter(f => f.length > 0);
}

function useExternalServer(): boolean {
    const config = vscode.workspace.getConfiguration('dynlex');
    return config.get<boolean>('server.useExternal') || false;
}

async function waitForPort(port: number, timeoutMs: number = 30000): Promise<boolean> {
    const startTime = Date.now();
    let attempt = 0;
    while (Date.now() - startTime < timeoutMs) {
        if (isShuttingDown) {
            return false;
        }
        attempt++;
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
            socket.connect(port, '127.0.0.1');
        });
        if (connected) {
            return true;
        }
        log(`Connection attempt ${attempt} failed, retrying...`);
        await new Promise(resolve => setTimeout(resolve, 200));
    }
    return false;
}

async function startLanguageServer(context: vscode.ExtensionContext) {
    const port = getServerPort();

    if (useExternalServer()) {
        log(`Waiting for external server on port ${port}...`);
        const ready = await waitForPort(port);
        if (!ready) {
            logError(`Timed out waiting for external server on port ${port}`);
            vscode.window.showErrorMessage(`Timed out waiting for DynLex language server on port ${port}`);
            return;
        }
        log(`External server is ready`);
    } else {
        const serverPath = getServerPath();
        const extraFlags = getServerFlags();
        const args = ['--lsp', ...extraFlags];

        log(`Server path resolved to: ${serverPath}`);
        log(`Starting language server: ${serverPath} ${args.join(' ')} on port ${port}`);

        // Spawn the server process
        serverProcess = spawn(serverPath, args, {
            stdio: ['ignore', 'pipe', 'pipe']
        });

        serverProcess.stdout?.on('data', (data) => {
            log(`Server stdout: ${data.toString().trim()}`);
        });

        serverProcess.stderr?.on('data', (data) => {
            log(`Server stderr: ${data.toString().trim()}`);
        });

        serverProcess.on('error', (err) => {
            logError(`Failed to start server: ${err.message}`);
            vscode.window.showErrorMessage(`Failed to start DynLex language server: ${err.message}`);
            scheduleReconnect(context);
        });

        serverProcess.on('exit', (code, signal) => {
            log(`Server process exited with code ${code}, signal ${signal}`);
            if (!isShuttingDown) {
                vscode.window.showWarningMessage(`DynLex language server exited unexpectedly.`);
                scheduleReconnect(context);
            }
        });

        // Give the server a moment to start listening
        await new Promise(resolve => setTimeout(resolve, 500));
    }

    // Connect to the server via TCP
    try {
        await connectToServer(port, context);
        reconnectAttempts = 0;
    } catch (err) {
        logError(`Error connecting to server: ${err}`);
        scheduleReconnect(context);
    }
}

async function connectToServer(port: number, context: vscode.ExtensionContext) {
    log(`Connecting to language server on port ${port}...`);

    const serverOptions = (): Promise<StreamInfo> => {
        return new Promise((resolve, reject) => {
            const socket = new net.Socket();

            socket.on('connect', () => {
                log('Connected to language server');
                resolve({
                    reader: socket,
                    writer: socket
                });
            });

            socket.on('error', (err) => {
                logError(`Socket error: ${err.message}`);
                reject(err);
            });

            socket.on('close', () => {
                log('Socket closed');
                if (!isShuttingDown && client) {
                    vscode.window.showErrorMessage('Connection to DynLex language server lost.');
                    scheduleReconnect(context);
                }
            });

            socket.connect(port, '127.0.0.1');
        });
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'dynlex' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.dl')
        },
        outputChannel: outputChannel
    };

    client = new LanguageClient(
        'dynlex-language-server',
        'DynLex Language Server',
        serverOptions,
        clientOptions
    );

    try {
        await client.start();
        log('Language client started successfully');
        context.subscriptions.push(client);
    } catch (err) {
        logError(`Failed to start language client: ${err}`);
        throw err;
    }
}

function scheduleReconnect(context: vscode.ExtensionContext) {
    if (isShuttingDown) {
        return;
    }

    if (reconnectTimeout) {
        clearTimeout(reconnectTimeout);
    }

    // Exponential backoff: 5s, 10s, 20s, 40s, 60s (capped)
    const delay = Math.min(BASE_RECONNECT_DELAY * Math.pow(2, reconnectAttempts), MAX_RECONNECT_DELAY);
    reconnectAttempts++;

    log(`Scheduling reconnect attempt ${reconnectAttempts} in ${delay / 1000} seconds...`);

    reconnectTimeout = setTimeout(async () => {
        log(`Attempting to reconnect (attempt ${reconnectAttempts})...`);
        await stopLanguageServer();
        await startLanguageServer(context);
    }, delay);
}

async function stopLanguageServer(): Promise<void> {
    if (client) {
        try {
            await client.stop();
            log('Language client stopped');
        } catch (err) {
            logError(`Error stopping client: ${err}`);
        }
        client = undefined;
    }

    if (serverProcess) {
        serverProcess.kill();
        serverProcess = undefined;
        log('Server process killed');
    }
}
