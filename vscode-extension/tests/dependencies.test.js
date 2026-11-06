const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const { createRequire } = require('node:module');
const vm = require('node:vm');
const { minimatch, Minimatch } = require('minimatch');

// Only VS Code's host API is substituted; exercise the installed client code.
function loadClientModule(name, vscode) {
    const filename = require.resolve(`vscode-languageclient/$test/common/${name}`);
    const localRequire = createRequire(filename);
    const module = { exports: {} };
    vm.compileFunction(fs.readFileSync(filename, 'utf8'), ['require', 'module', 'exports'], { filename })(
        name => name === 'vscode' ? vscode : localRequire(name), module, module.exports,
    );
    return module.exports;
}

test('patched glob matching preserves language-client and packaging patterns', () => {
    const patterns = [
        ['src/main.dl', '**/*.dl', true],
        ['src/main.cpp', '**/*.dl', false],
        ['src/nested/main.dl', '**/*.{dl,json}', true],
        ['.vscode/settings.json', '**/*.json', true, { dot: true }],
        ['node_modules/client/index.js', 'node_modules/**', true],
        ['src/editor.ts', '**/*.ts', true],
        ['SRC/MAIN.DL', '**/*.dl', true, { nocase: true }],
    ];
    for (const [file, pattern, expected, options] of patterns) {
        assert.equal(minimatch(file, pattern, options), expected);
        const matcher = new Minimatch(pattern, options);
        assert.equal(matcher.match(file), expected);
    }
});

test('glob preprocessing stops pattern amplification at the configured budget', () => {
    assert.throws(() => new Minimatch('a/' + '**/../x/y/'.repeat(4) + 'z', {
        optimizationLevel: 2,
        braceExpandMax: 8,
    }), { name: 'RangeError', message: /too many patterns/ });
    const ordinary = new Minimatch('src/**/main.dl', { optimizationLevel: 2 });
    assert.equal(ordinary.match('src/nested/main.dl'), true);
});

test('language-client glob selectors preserve matching and invalid-pattern handling', () => {
    const { matchGlobPattern } = loadClientModule('utils/globPattern', {
        Uri: { parse: value => {
            const uri = new URL(value);
            return { path: uri.pathname, with: ({ path }) => ({ fsPath: path }) };
        } },
    });
    assert.equal(matchGlobPattern('**/*.dl', { fsPath: '/workspace/main.dl' }), true);
    assert.equal(matchGlobPattern('**/*.dl', { fsPath: '/workspace/main.cpp' }), false);
    assert.equal(matchGlobPattern('**\\*.dl', { fsPath: '/workspace/main.dl' }), true);
    assert.equal(matchGlobPattern({ baseUri: 'file:///workspace', pattern: '*.dl' },
        { fsPath: '/workspace/main.dl' }), true);
    assert.equal(matchGlobPattern('# comment', { fsPath: '/workspace/main.dl' }), false);
    assert.equal(matchGlobPattern('', { fsPath: '' }), false);
});

test('language-client file-operation registration preserves filters and errors', async () => {
    const { DidCreateFilesFeature } = loadClientModule('fileOperations', {
        workspace: { onDidCreateFiles: () => ({ dispose() {} }) },
    });
    const feature = new DidCreateFilesFeature({
        code2ProtocolConverter: { asDidCreateFilesParams: event => event },
    });
    feature.register({ id: 'dynlex', registerOptions: { filters: [{
        scheme: 'file', pattern: { glob: '**/*.dl', options: { ignoreCase: true } },
    }] } });
    const files = [
        { scheme: 'file', fsPath: '/workspace/main.DL' },
        { scheme: 'file', fsPath: '/workspace/.hidden.dl' },
        { scheme: 'file', fsPath: '/workspace/main.cpp' },
        { scheme: 'https', fsPath: '/workspace/main.dl' },
    ];
    assert.deepEqual((await feature.filter({ files }, uri => uri)).files, files.slice(0, 2));
    for (const glob of ['', '# comment']) {
        assert.throws(() => feature.register({ id: 'invalid', registerOptions: {
            filters: [{ pattern: { glob } }],
        } }), /Invalid pattern/);
    }
    feature.clear();
});
