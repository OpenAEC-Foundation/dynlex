const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const extensionRoot = path.resolve(__dirname, '..');
const manifest = require('../package.json');

test('extension manifest includes the DynLex icon at marketplace resolution', () => {
    assert.equal(manifest.icon, 'icons/dynlex.png');
    assert.deepEqual(manifest.contributes.languages[0].icon, {
        light: './icons/dynlex.png',
        dark: './icons/dynlex.png',
    });

    const icon = fs.readFileSync(path.join(extensionRoot, manifest.icon));
    assert.deepEqual(icon.subarray(0, 8), Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]));
    assert.equal(icon.toString('ascii', 12, 16), 'IHDR');
    assert.equal(icon.readUInt32BE(16), 256);
    assert.equal(icon.readUInt32BE(20), 256);
    assert.equal(
        crypto.createHash('sha256').update(icon).digest('hex'),
        '50943a596e4bea48c7e48edf04efa1f71425972bd2a8ccaedafa720c28df5fbd',
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
