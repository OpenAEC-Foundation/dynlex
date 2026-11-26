const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const metadata = require('../package.json').contributes;

test('library management is available from Explorer, its title button and library context menus', () => {
    assert(metadata.views.explorer.some(view => view.id === 'dynlex.libraries'));
    assert(metadata.menus['view/title'].some(item => item.command === 'dynlex.addLibrary'));
    const actions = metadata.menus['view/item/context'].map(item => item.command);
    for (const name of ['updateLibrary', 'removeLibrary', 'libraryDocumentation']) assert(actions.includes('dynlex.' + name));
    for (const command of ['dynlex.addLibrary', ...actions, 'dynlex.restoreLibraries'])
        assert(metadata.commands.some(item => item.command === command));
});
test('the packaged extension includes the shared package manager and archive integrity checks', () => {
    const bundle = fs.readFileSync(require.resolve('../dist/extension.js'), 'utf8');
    assert(bundle.includes('Library integrity check failed'));
    assert(bundle.includes('dynlex.addLibrary'));
    assert(bundle.includes('dynlex.lock'));
});
