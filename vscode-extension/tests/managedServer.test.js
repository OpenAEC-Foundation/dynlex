const assert = require('node:assert/strict');
const test = require('node:test');

const { buildManagedServerOptions } = require('../out/managedServer');

test('managed language servers use an isolated stdio process', () => {
    const options = buildManagedServerOptions(
        '/workspace/build/dynlex',
        ['--lsp-trace=/tmp/dynlex-lsp.log'],
        '/workspace',
    );

    assert.deepEqual(options, {
        command: '/workspace/build/dynlex',
        args: ['--stdio', '--lsp-trace=/tmp/dynlex-lsp.log'],
        options: { cwd: '/workspace' },
    });
    assert.equal(options.args.includes('--lsp'), false);
    assert.equal(options.args.includes('--port'), false);
});
