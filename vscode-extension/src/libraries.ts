import * as vscode from 'vscode';
import * as path from 'node:path';
import {aliasName, defaultCatalog, sourceFromUrl} from '../../packages/manager.mjs';
import type {CatalogEntry, Source} from '../../packages/manager.mjs';
import {nodeManager} from '../../packages/node.mjs';

class Library extends vscode.TreeItem {
    constructor(readonly folder: vscode.WorkspaceFolder, readonly alias: string,
        readonly documentation: string, revision: string, name: string) {
        super(alias, vscode.TreeItemCollapsibleState.None);
        this.description = `${name} · ${revision === 'local' ? revision : revision.slice(0, 12)}`;
        this.tooltip = `${name}\nImported as ${alias}\n${revision}\nProject: ${folder.uri.fsPath}`;
        this.contextValue = 'dynlexLibrary';
        this.iconPath = new vscode.ThemeIcon('library');
    }
}

export function registerLibraries(context: vscode.ExtensionContext, refreshLanguage: () => void) {
    let languageTimer: ReturnType<typeof setTimeout> | undefined;
    const refresh = () => {
        if (languageTimer) clearTimeout(languageTimer);
        languageTimer = setTimeout(refreshLanguage, 200);
    };
    context.subscriptions.push({dispose() { if (languageTimer) clearTimeout(languageTimer); }});
    const changes = new vscode.EventEmitter<void>();
    const log = vscode.window.createOutputChannel('DynLex Libraries');
    const managers = new Map<string, ReturnType<typeof nodeManager>>();
    const manager = (folder: vscode.WorkspaceFolder) => {
        const catalog = vscode.workspace.getConfiguration('dynlex', folder.uri).get<string>('libraries.catalog', defaultCatalog);
        const key = folder.uri.toString() + '\n' + catalog;
        if (!managers.has(key)) managers.set(key, nodeManager(folder.uri.fsPath, catalog));
        return managers.get(key)!;
    };
    const folders = () => (vscode.workspace.workspaceFolders || []).filter(folder => folder.uri.scheme === 'file');
    async function projects() {
        const found = new Map(folders().map(folder => [folder.uri.fsPath, folder]));
        const manifests = await vscode.workspace.findFiles('**/dynlex.json', '**/{.git,.dynlex,.worktrees,node_modules,build}/**');
        for (const manifest of manifests) {
            const owner = vscode.workspace.getWorkspaceFolder(manifest);
            if (!owner || manifest.scheme !== 'file') continue;
            const directory = path.dirname(manifest.fsPath);
            found.set(directory, {...owner, uri: vscode.Uri.file(directory), name: path.basename(directory)});
        }
        return [...found.values()];
    }
    const provider: vscode.TreeDataProvider<Library> = {
        onDidChangeTreeData: changes.event,
        getTreeItem: item => item,
        async getChildren() {
            const libraries: Library[] = [];
            const available = await projects();
            for (const folder of available) {
                const project = await manager(folder).read();
                for (const [alias, name] of Object.entries(project.lock.dependencies)) {
                    const entry = project.lock.packages[name];
                    const library = new Library(folder, alias, entry.documentation, entry.revision, name);
                    if (available.length > 1) library.description = `${folder.name} · ${library.description}`;
                    libraries.push(library);
                }
            }
            return libraries;
        }
    };
    context.subscriptions.push(changes, log, vscode.window.registerTreeDataProvider('dynlex.libraries', provider));
    const watcher = vscode.workspace.createFileSystemWatcher('**/{dynlex.json,dynlex.lock}');
    for (const event of [watcher.onDidChange, watcher.onDidCreate, watcher.onDidDelete])
        context.subscriptions.push(event(() => { changes.fire(); refresh(); }));
    context.subscriptions.push(watcher);
    async function selectFolder(explicit?: vscode.Uri) {
        const available = await projects();
        if (!available.length) throw new Error('Open a local application folder before adding a library');
        if (explicit) {
            const owner = vscode.workspace.getWorkspaceFolder(explicit);
            if (!owner || explicit.scheme !== 'file') throw new Error('Choose a local application folder');
            return {...owner, uri: explicit, name: path.basename(explicit.fsPath)};
        }
        const active = vscode.window.activeTextEditor?.document.uri;
        if (active?.scheme === 'file') {
            const containing = available.filter(folder => {
                const relative = path.relative(folder.uri.fsPath, active.fsPath);
                return relative !== '..' && !relative.startsWith('..' + path.sep) && !path.isAbsolute(relative);
            }).sort((left, right) => right.uri.fsPath.length - left.uri.fsPath.length);
            if (containing.length) return containing[0];
        }
        if (available.length === 1) return available[0];
        const choice = await vscode.window.showQuickPick(available.map(folder => ({label: folder.name, description: folder.uri.fsPath, folder})),
            {placeHolder: 'Choose the application project'});
        return choice?.folder;
    }
    function command(name: string, action: (...args: unknown[]) => Promise<void>) {
        context.subscriptions.push(vscode.commands.registerCommand(name, async (...args: unknown[]) => {
            try { await action(...args); }
            catch (error) {
                log.appendLine(String(error));
                void vscode.window.showErrorMessage(String(error), 'Open log').then(choice => { if (choice) log.show(); });
            }
        }));
    }
    async function install(folder: vscode.WorkspaceFolder, source: Source, suggested: string, update = false) {
        const alias = update ? suggested : await vscode.window.showInputBox({prompt: 'Import this library as', value: suggested,
            validateInput: value => { try { aliasName(value); return undefined; } catch (error) { return String(error); } }});
        if (!alias) return;
        if (!update && Object.prototype.hasOwnProperty.call((await manager(folder).read()).manifest.dependencies, alias))
            throw new Error(`The project already has a library named ${alias}`);
        await vscode.window.withProgress({location: vscode.ProgressLocation.Notification, title: `${update ? 'Updating' : 'Adding'} library ${alias}`},
            () => manager(folder).add(source, alias, update));
        changes.fire(); refresh();
        void vscode.window.showInformationMessage(`Library ${alias} is ready. Import ${alias}/… in your source.`);
    }
    command('dynlex.addLibrary', async preferred => {
        const folder = await selectFolder(preferred instanceof vscode.Uri ? preferred : undefined); if (!folder) return;
        type Choice = vscode.QuickPickItem & {entry?: CatalogEntry; kind_?: string};
        const custom: Choice[] = [{label: 'Add from repository or archive URL…', kind_: 'url'}, {label: 'Browse local folder…', kind_: 'folder'}];
        const picker = vscode.window.createQuickPick<Choice>();
        picker.title = 'Add Library'; picker.placeholder = 'Search libraries to add to your project';
        picker.matchOnDescription = true; picker.matchOnDetail = true; picker.items = custom; picker.busy = true;
        let closed = false;
        const picked = await new Promise<Choice | undefined>(resolve => {
            picker.onDidAccept(() => resolve(picker.selectedItems[0]));
            picker.onDidHide(() => resolve(undefined));
            void manager(folder).catalog().then(libraries => {
                if (closed) return;
                const choices: Choice[] = libraries.map(entry => ({label: entry.title, description: entry.publisher, detail: entry.description, entry}));
                picker.items = [...choices, ...custom]; picker.busy = false;
                const match = typeof preferred === 'string' ? choices.find(item => item.entry?.alias === preferred) : undefined;
                if (match) resolve(match);
            }).catch(error => {
                log.appendLine(String(error));
                if (!closed) { picker.busy = false; picker.placeholder = 'Catalog unavailable; check DynLex Libraries output. Add a URL or folder below.'; }
            });
            picker.show();
        });
        closed = true; picker.hide(); picker.dispose();
        if (!picked) return;
        if (picked.entry) { await install(folder, {catalog: picked.entry.name}, typeof preferred === 'string' ? preferred : picked.entry.alias); return; }
        if ('kind_' in picked && picked.kind_ === 'folder') {
            const chosen = await vscode.window.showOpenDialog({canSelectFolders: true, canSelectFiles: false, canSelectMany: false,
                openLabel: 'Add library'});
            if (!chosen?.length) return;
            await install(folder, {path: path.relative(folder.uri.fsPath, chosen[0].fsPath)}, path.basename(chosen[0].fsPath).replace(/^dynlex-/, ''));
        } else {
            const url = await vscode.window.showInputBox({prompt: 'Library Git repository or SDK archive URL', placeHolder: 'https://…'});
            if (!url) return;
            const parsed = new URL(url);
            const leaf = parsed.pathname.split('/').filter(Boolean).at(-1);
            if (!leaf) throw new Error('A library URL must identify a repository or SDK archive');
            const name = leaf.replace(/\.(git|zip)$/, '').replace(/^dynlex-/, '');
            await install(folder, sourceFromUrl(url), name);
        }
    });
    command('dynlex.removeLibrary', async item => {
        if (!(item instanceof Library)) return;
        await manager(item.folder).remove(item.alias); changes.fire(); refresh();
    });
    command('dynlex.updateLibrary', async item => {
        if (!(item instanceof Library)) return;
        const project = await manager(item.folder).read();
        await install(item.folder, project.manifest.dependencies[item.alias], item.alias, true);
    });
    command('dynlex.libraryDocumentation', async item => {
        if (!(item instanceof Library)) return;
        if (!item.documentation) throw new Error('This library does not declare a documentation URL');
        await vscode.env.openExternal(vscode.Uri.parse(item.documentation));
    });
    command('dynlex.restoreLibraries', async () => {
        const folder = await selectFolder(); if (!folder) return;
        await vscode.window.withProgress({location: vscode.ProgressLocation.Notification, title: 'Restoring locked libraries'},
            () => manager(folder).restore());
        changes.fire(); refresh();
    });
}
