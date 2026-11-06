const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const extensionRoot = path.resolve(__dirname, '..');
const manifest = require('../package.json');

test('nested call colors use the shared palette and stay scoped to DynLex', () => {
    const colors = require('../../shared/call-colors.json');
    const rules = manifest.contributes.configurationDefaults['editor.semanticTokenColorCustomizations'].rules;
    assert.deepEqual(manifest.contributes.semanticTokenModifiers.map(entry => entry.id), colors.map((_, depth) => `callDepth${depth}`));
    for (const [depth, color] of colors.entries()) {
        for (const type of ['function', 'intrinsic', 'type']) {
            assert.deepEqual(rules[`${type}.callDepth${depth}:dynlex`], { foreground: `#${color.editor}` });
        }
    }
});

test('extension uses the shared SVG file icon and a marketplace-resolution PNG', () => {
    assert.equal(manifest.icon, 'icons/dynlex.png');
    assert.deepEqual(manifest.contributes.languages[0].icon, {
        light: './icons/dynlex.svg',
        dark: './icons/dynlex.svg',
    });
    assert.deepEqual(
        fs.readFileSync(path.join(extensionRoot, 'icons/dynlex.svg')),
        fs.readFileSync(path.join(extensionRoot, '../web/icons/dynlex-icon.svg')),
    );

    const icon = fs.readFileSync(path.join(extensionRoot, manifest.icon));
    assert.deepEqual(icon.subarray(0, 8), Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]));
    assert.equal(icon.toString('ascii', 12, 16), 'IHDR');
    assert.equal(icon.readUInt32BE(16), 256);
    assert.equal(icon.readUInt32BE(20), 256);
    assert.equal(
        crypto.createHash('sha256').update(icon).digest('hex'),
        '39c0d5767becb525ebd8d8d24d74bd8f12d1d12894791e9d6d32c2f6ed83902f',
    );
});

test('extension manifest has complete registry metadata', () => {
    assert.equal(manifest.publisher, 'impertio');
    assert.equal(manifest.license, 'LGPL-3.0-or-later');
    assert.deepEqual(manifest.repository, {
        type: 'git',
        url: 'https://github.com/OpenAEC-Foundation/dynlex.git',
        directory: 'vscode-extension',
    });
    assert.equal(manifest.homepage, 'https://dynlex.com');
    assert.deepEqual(manifest.bugs, {
        url: 'https://github.com/OpenAEC-Foundation/dynlex/issues',
    });
});
