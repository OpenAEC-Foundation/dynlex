import test from 'node:test';
import assert from 'node:assert/strict';
import {bytes} from '../archive.mjs';
import {packageFromFolder, emptyProject} from '../manager.mjs';
import {createLibraryServices} from '../browser.mjs';

test('browser folder installation survives service recreation and removes all unused sources', async () => {
  const storage = new Map(), files = new Map([
    ['dynlex-package.json', bytes(JSON.stringify({schema: 1, name: 'example', exports: {'*': 'ui/*'}, files: ['ui']}))],
    ['ui/main.dl', bytes('answer means: 42')], ['ui/pixels.bin', new Uint8Array([0, 255, 1])]
  ]);
  const options = {storage: async (key, value) => value ? storage.set(key, value) : storage.get(key), selectFolder: async () => files};
  const signal = new AbortController().signal;
  const first = createLibraryServices('https://example.com/index.json', options);
  const added = await first.libraries({operation: 'folder', alias: 'ui', project: emptyProject()}, {signal});
  assert.equal(added.sources['.dynlex/packages/example/ui/main.dl'], 'answer means: 42');
  const second = createLibraryServices('https://example.com/index.json', options);
  const restored = await second.libraries({operation: 'restore', project: added.project}, {signal});
  assert.deepEqual(restored.sources, added.sources);
  const runtime = await second.runtimeFiles(added.project);
  assert.deepEqual([...runtime['.dynlex/packages/example/ui/pixels.bin']], [0, 255, 1]);
  const removed = await second.libraries({operation: 'remove', alias: 'ui', project: added.project}, {signal});
  assert.deepEqual(Object.keys(removed.sources).sort(), ['dynlex.json', 'dynlex.lock']);
  assert.deepEqual(removed.project.lock.packages, {});
});
test('browser restore verifies cached source integrity and reports missing local packages', async () => {
  const package_ = await packageFromFolder(new Map([
    ['dynlex-package.json', bytes(JSON.stringify({schema: 1, name: 'example', exports: {'*': 'ui/*'}, files: ['ui']}))],
    ['ui/main.dl', bytes('answer means: 42')]
  ]), 'local');
  const project = emptyProject(), source = {local: package_.manifest.contentHash};
  project.manifest.dependencies.ui = source;
  project.lock.dependencies.ui = 'example'; project.lock.requests.ui = source;
  project.lock.packages.example = {root: '.dynlex/packages/example', contentHash: package_.manifest.contentHash,
    exports: {'*': 'ui/*'}, source, dependencies: {}, revision: 'local'};
  const signal = new AbortController().signal;
  const missing = createLibraryServices('https://example.com', {storage: async () => undefined});
  await assert.rejects(missing.libraries({operation: 'restore', project}, {signal}), /no longer available/);
  package_.files.set('ui/main.dl', bytes('corrupt'));
  const corrupt = createLibraryServices('https://example.com', {storage: async () => ({files: package_.files})});
  await assert.rejects(corrupt.libraries({operation: 'restore', project}, {signal}), /integrity/);
});
