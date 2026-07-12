import type { Executable } from 'vscode-languageclient/node';

export function buildManagedServerOptions(
    serverPath: string,
    extraFlags: string[],
    cwd: string | undefined,
): Executable {
    return {
        command: serverPath,
        args: ['--stdio', ...extraFlags],
        options: { cwd },
    };
}
