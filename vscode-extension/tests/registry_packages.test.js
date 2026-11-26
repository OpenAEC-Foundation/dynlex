const assert = require('node:assert/strict');
const path = require('node:path');
const test = require('node:test');
const { pathToFileURL } = require('node:url');
const { execFileSync } = require('node:child_process');
const { mkdtemp, rm } = require('node:fs/promises');
const os = require('node:os');
const { readVSIXPackage, readZip } = require('@vscode/vsce/out/zip');

const manifest = require('../package.json');
const packagingModuleUrl = pathToFileURL(
    path.resolve(__dirname, '../scripts/package-registries.mjs'),
).href;

test('registry packages have distinct, explicit extension identities', async () => {
    const {
        artifactFileName,
        buildRegistryManifest,
        parseProductionDependencyDirectories,
        registryPackages,
        selectRegistryPackages,
    } = await import(packagingModuleUrl);

    assert.deepEqual(registryPackages, [
        {
            registry: 'vscode-marketplace',
            publisher: 'impertio',
        },
        {
            registry: 'open-vsx',
            publisher: 'open-aec',
        },
    ]);

    for (const registryPackage of registryPackages) {
        const registryManifest = buildRegistryManifest(manifest, registryPackage);
        assert.equal(registryManifest.publisher, registryPackage.publisher);
        assert.equal(registryManifest.name, 'dynlex-language');
        assert.equal(registryManifest.version, manifest.version);
        assert.equal(
            artifactFileName(registryManifest, registryPackage),
            `dynlex-language-${manifest.version}-${registryPackage.registry}.vsix`,
        );

        const unchangedFields = { ...manifest };
        const packagedFields = { ...registryManifest };
        delete unchangedFields.publisher;
        delete packagedFields.publisher;
        assert.deepEqual(packagedFields, unchangedFields);
    }

    assert.deepEqual(
        selectRegistryPackages('open-vsx'),
        [registryPackages[1]],
    );
    assert.throws(
        () => selectRegistryPackages('unknown-registry'),
        /Unknown extension registry 'unknown-registry'/,
    );

    const extensionDirectory = path.resolve('/workspace/vscode-extension');
    assert.deepEqual(
        parseProductionDependencyDirectories(
            [
                extensionDirectory,
                path.join(extensionDirectory, 'node_modules/example'),
                path.join(
                    extensionDirectory,
                    'node_modules/example/node_modules/nested',
                ),
                '',
            ].join('\n'),
            extensionDirectory,
        ),
        [
            path.join(extensionDirectory, 'node_modules/example'),
            path.join(
                extensionDirectory,
                'node_modules/example/node_modules/nested',
            ),
        ],
    );
    assert.throws(
        () => parseProductionDependencyDirectories(
            `${extensionDirectory}\n/workspace/unrelated`,
            extensionDirectory,
        ),
        /outside the extension's node_modules directory/,
    );
});

test('a staged VSIX bundles the shared library manager and contributes its commands', async () => {
    const directory = await mkdtemp(path.join(os.tmpdir(), 'dynlex-vsix-test-'));
    try {
        execFileSync(process.execPath, [
            path.resolve(__dirname, '../scripts/package-registries.mjs'),
            '--registry', 'vscode-marketplace', '--out-dir', directory,
        ], { encoding: 'utf8', timeout: 60000 });
        const filename = path.join(directory, `dynlex-language-${manifest.version}-vscode-marketplace.vsix`);
        const packaged = await readVSIXPackage(filename);
        const command = 'dynlex.addLibrary';
        assert(packaged.manifest.contributes.commands.some(entry => entry.command === command));
        const files = await readZip(filename, name => name === 'extension/dist/extension.js');
        assert(files.get('extension/dist/extension.js').includes(command));
    } finally {
        await rm(directory, { recursive: true, force: true });
    }
});
