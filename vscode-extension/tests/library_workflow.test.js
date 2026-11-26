const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const http = require('node:http');
const {execFileSync} = require('node:child_process');
const Module = require('node:module');

test('Explorer adds, updates, restores and removes a catalog library in an application folder', async () => {
    const {packageFromFolder} = await import('../../packages/manager.mjs');
    const {bytes} = await import('../../packages/archive.mjs');
    const temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'dynlex-vscode-library-'));
    const root = path.join(temporary, 'app'); await fs.mkdir(root);
    let archive, hash;
    async function release(contents) {
        const package_ = await packageFromFolder(new Map([
            ['dynlex-package.json', bytes(JSON.stringify({schema: 1, name: 'example', files: ['ui'], exports: {'*': 'ui/*'}}))],
            ['ui/native.dl', bytes(contents)]
        ]), contents);
        const folder = path.join(temporary, 'sdk'); await fs.mkdir(folder, {recursive: true});
        for (const [name, data] of package_.files) {
            const target = path.join(folder, name); await fs.mkdir(path.dirname(target), {recursive: true}); await fs.writeFile(target, data);
        }
        const output = path.join(temporary, 'library.zip');
        execFileSync('python3', ['-c', 'import sys,zipfile,pathlib\nr=pathlib.Path(sys.argv[1])\nwith zipfile.ZipFile(sys.argv[2],"w",compression=zipfile.ZIP_DEFLATED) as z:\n for p in r.rglob("*"):\n  if p.is_file(): z.write(p,p.relative_to(r))', folder, output]);
        archive = await fs.readFile(output); hash = package_.manifest.contentHash;
    }
    await release('answer means: 42');
    const server = http.createServer((request, response) => {
        if (request.url === '/library.zip') { response.end(archive); return; }
        response.setHeader('Content-Type', 'application/json');
        response.end(JSON.stringify({schema: 1, libraries: [{name: 'example', alias: 'ui', title: 'Example UI',
            publisher: 'Test publisher', description: 'A test library', source: {url: 'library.zip', contentHash: hash}}]}));
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const catalog = `http://127.0.0.1:${server.address().port}/index.json`;
    const commands = new Map(), errors = [], subscriptions = [];
    let provider;
    const disposable = () => ({dispose() {}});
    const folder = {name: 'app', uri: {fsPath: root, scheme: 'file', toString: () => 'file://' + root}};
    class TreeItem { constructor(label) { this.label = label; } }
    class EventEmitter { constructor() { this.event = () => disposable(); } fire() {} dispose() {} }
    class Uri { static file(value) { return {fsPath: value, scheme: 'file', toString: () => 'file://' + value}; } }
    const mock = {
        TreeItem, EventEmitter, Uri, TreeItemCollapsibleState: {None: 0}, ThemeIcon: class {},
        ProgressLocation: {Notification: 1},
        workspace: {workspaceFolders: [folder], getConfiguration: () => ({get: () => catalog}), findFiles: async () => [], getWorkspaceFolder: () => folder,
            createFileSystemWatcher: () => ({onDidChange: disposable, onDidCreate: disposable, onDidDelete: disposable, dispose() {}})},
        commands: {registerCommand(name, callback) { commands.set(name, callback); return disposable(); }},
        window: {
            createOutputChannel: () => ({appendLine() {}, show() {}, dispose() {}}),
            registerTreeDataProvider(name, value) { assert.equal(name, 'dynlex.libraries'); provider = value; return disposable(); },
            showInputBox: async () => 'ui', withProgress: (_, callback) => callback(),
            showInformationMessage: async () => undefined,
            showErrorMessage: async message => { errors.push(message); },
            createQuickPick() {
                let accept, hide, items = [];
                return {get items() { return items; }, set items(value) { items = value; if (value.some(item => item.entry)) accept(); },
                    get selectedItems() { return items.filter(item => item.entry).slice(0, 1); },
                    onDidAccept(callback) { accept = callback; return disposable(); },
                    onDidHide(callback) { hide = callback; return disposable(); }, show() {}, hide() { hide(); }, dispose() {}};
            }
        }
    };
    const originalLoad = Module._load;
    Module._load = function(name, ...args) { return name === 'vscode' ? mock : originalLoad.call(this, name, ...args); };
    try {
        const {registerLibraries} = require('../out/libraries');
        registerLibraries({subscriptions}, () => {});
        await commands.get('dynlex.addLibrary')();
        assert.deepEqual(errors, []);
        let libraries = await provider.getChildren();
        assert.equal(libraries.length, 1); assert.equal(libraries[0].alias, 'ui');
        const installed = path.join(root, '.dynlex/packages/example/ui/native.dl');
        assert.equal(await fs.readFile(installed, 'utf8'), 'answer means: 42');
        assert.equal(JSON.parse(await fs.readFile(path.join(root, 'dynlex.json'), 'utf8')).dependencies.ui.catalog, 'example');
        await release('answer means: 84');
        await commands.get('dynlex.updateLibrary')(libraries[0]);
        assert.equal(await fs.readFile(installed, 'utf8'), 'answer means: 84');
        await fs.rm(path.join(root, '.dynlex/packages'), {recursive: true});
        await commands.get('dynlex.restoreLibraries')();
        assert.equal(await fs.readFile(installed, 'utf8'), 'answer means: 84');
        await commands.get('dynlex.removeLibrary')(libraries[0]);
        libraries = await provider.getChildren();
        assert.equal(libraries.length, 0);
        assert.deepEqual(await fs.readdir(path.join(root, '.dynlex/packages')), []);
        assert.deepEqual(errors, []);
    } finally {
        Module._load = originalLoad;
        for (const subscription of subscriptions) subscription.dispose();
        await new Promise(resolve => server.close(resolve));
        await fs.rm(temporary, {recursive: true, force: true});
    }
});
