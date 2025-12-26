import * as path from 'path';
import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

/**
 * Activates the 3BX language extension.
 * Sets up the Language Server Protocol client if enabled.
 */
export async function activate(context: vscode.ExtensionContext): Promise<void> {
    console.log('3BX extension is now active');

    // Register commands
    context.subscriptions.push(
        vscode.commands.registerCommand('3bx.restartServer', restartServer),
        vscode.commands.registerCommand('3bx.compileFile', compileCurrentFile)
    );

    // Start LSP client if enabled
    const config = vscode.workspace.getConfiguration('3bx');
    if (config.get<boolean>('lsp.enabled', true)) {
        await startLanguageClient(context);
    }

    // Watch for configuration changes
    context.subscriptions.push(
        vscode.workspace.onDidChangeConfiguration(async (e) => {
            if (e.affectsConfiguration('3bx.lsp.enabled') ||
                e.affectsConfiguration('3bx.compiler.path')) {
                await restartServer();
            }
        })
    );
}

/**
 * Deactivates the extension and stops the language client.
 */
export async function deactivate(): Promise<void> {
    if (client) {
        await client.stop();
        client = undefined;
    }
}

/**
 * Starts the Language Server Protocol client.
 * Spawns the 3BX compiler with --lsp flag as the server.
 */
async function startLanguageClient(context: vscode.ExtensionContext): Promise<void> {
    const config = vscode.workspace.getConfiguration('3bx');

    // Get compiler path from settings or use default
    let compilerPath = config.get<string>('compiler.path', '');
    if (!compilerPath) {
        compilerPath = 'threebx';  // Use from PATH
    }

    // Server options - spawn the compiler with --lsp flag
    const serverOptions: ServerOptions = {
        run: {
            command: compilerPath,
            args: ['--lsp'],
            transport: TransportKind.stdio
        },
        debug: {
            command: compilerPath,
            args: ['--lsp', '--debug'],
            transport: TransportKind.stdio
        }
    };

    // Client options
    const clientOptions: LanguageClientOptions = {
        // Register the server for 3bx documents
        documentSelector: [{ scheme: 'file', language: '3bx' }],
        synchronize: {
            // Notify the server about file changes to .3bx files
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.3bx')
        },
        outputChannel: vscode.window.createOutputChannel('3BX Language Server'),
        traceOutputChannel: vscode.window.createOutputChannel('3BX Language Server Trace'),
        initializationOptions: {
            // Pass any initialization options to the server
            trace: config.get<string>('lsp.trace.server', 'off')
        }
    };

    // Create the language client
    client = new LanguageClient(
        '3bx',
        '3BX Language Server',
        serverOptions,
        clientOptions
    );

    try {
        // Start the client (also launches the server)
        await client.start();
        console.log('3BX Language Server started successfully');
    } catch (error) {
        // Handle case where compiler doesn't exist or doesn't support --lsp
        const errorMessage = error instanceof Error ? error.message : String(error);

        if (errorMessage.includes('ENOENT') || errorMessage.includes('spawn')) {
            vscode.window.showWarningMessage(
                `3BX Language Server: Could not find compiler at '${compilerPath}'. ` +
                'Syntax highlighting will still work, but LSP features are disabled. ' +
                'Configure the compiler path in settings or install the 3BX compiler.'
            );
        } else {
            vscode.window.showWarningMessage(
                `3BX Language Server failed to start: ${errorMessage}. ` +
                'The compiler may not support --lsp yet. Syntax highlighting will still work.'
            );
        }

        client = undefined;
    }
}

/**
 * Restarts the language server.
 */
async function restartServer(): Promise<void> {
    const config = vscode.workspace.getConfiguration('3bx');

    if (client) {
        await client.stop();
        client = undefined;
    }

    if (config.get<boolean>('lsp.enabled', true)) {
        const context = getExtensionContext();
        if (context) {
            await startLanguageClient(context);
            vscode.window.showInformationMessage('3BX Language Server restarted');
        }
    }
}

// Store extension context for later use
let extensionContext: vscode.ExtensionContext | undefined;

/**
 * Gets the stored extension context.
 */
function getExtensionContext(): vscode.ExtensionContext | undefined {
    return extensionContext;
}

// Override activate to store context
const originalActivate = activate;
export { originalActivate };

/**
 * Compiles the currently open 3BX file.
 */
async function compileCurrentFile(): Promise<void> {
    const editor = vscode.window.activeTextEditor;

    if (!editor) {
        vscode.window.showWarningMessage('No file is currently open');
        return;
    }

    if (editor.document.languageId !== '3bx') {
        vscode.window.showWarningMessage('Current file is not a 3BX file');
        return;
    }

    // Save the document first
    await editor.document.save();

    const config = vscode.workspace.getConfiguration('3bx');
    let compilerPath = config.get<string>('compiler.path', '');
    if (!compilerPath) {
        compilerPath = 'threebx';
    }

    const filePath = editor.document.uri.fsPath;

    // Create output channel for compiler output
    const outputChannel = vscode.window.createOutputChannel('3BX Compiler');
    outputChannel.show();
    outputChannel.appendLine(`Compiling: ${filePath}`);
    outputChannel.appendLine('---');

    // Run the compiler
    const { exec } = require('child_process');

    exec(`"${compilerPath}" "${filePath}"`, (error: Error | null, stdout: string, stderr: string) => {
        if (stdout) {
            outputChannel.appendLine(stdout);
        }
        if (stderr) {
            outputChannel.appendLine(stderr);
        }
        if (error) {
            outputChannel.appendLine(`\nCompilation failed with exit code: ${error.message}`);
            vscode.window.showErrorMessage('3BX compilation failed. See output for details.');
        } else {
            outputChannel.appendLine('\nCompilation successful!');
            vscode.window.showInformationMessage('3BX compilation successful!');
        }
    });
}
