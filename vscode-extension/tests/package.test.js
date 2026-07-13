const assert = require('node:assert/strict');
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
});
