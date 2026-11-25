import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {nodeManager} from '../node.mjs';

test('local package file declarations reject symlinked parent directories', async () => {
  const temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'dynlex-library-symlink-'));
  try {
    const project = path.join(temporary, 'app'), library = path.join(temporary, 'library'), outside = path.join(temporary, 'outside');
    await Promise.all([project, library, outside].map(directory => fs.mkdir(directory)));
    await fs.writeFile(path.join(outside, 'main.dl'), 'answer means: 42');
    await fs.symlink(outside, path.join(library, 'linked'), 'junction');
    await fs.writeFile(path.join(library, 'dynlex-package.json'), JSON.stringify({schema: 1, name: 'example',
      files: ['linked/main.dl'], exports: {'main.dl': 'linked/main.dl'}}));
    await assert.rejects(nodeManager(project).add({path: '../library'}, 'ui'), /symlinks/);
  } finally { await fs.rm(temporary, {recursive: true, force: true}); }
});

test('Restore Libraries resolves a manifest without a lock and removes deleted requests', async () => {
  const temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'dynlex-restore-test-'));
  try {
    const project = path.join(temporary, 'app'), library = path.join(temporary, 'library');
    await fs.mkdir(project); await fs.mkdir(library);
    await fs.writeFile(path.join(library, 'dynlex-package.json'), JSON.stringify({schema: 1, name: 'example', files: ['main.dl'], exports: {'main.dl': 'main.dl'}}));
    await fs.writeFile(path.join(library, 'main.dl'), 'answer means: 42');
    await fs.writeFile(path.join(project, 'dynlex.json'), JSON.stringify({schema: 1, dependencies: {ui: {path: '../library'}}}));
    const manager = nodeManager(project);
    await assert.rejects(manager.read(), /restored/);
    await manager.restore();
    assert.equal((await manager.read()).lock.dependencies.ui, 'example');
    await fs.writeFile(path.join(project, 'dynlex.json'), JSON.stringify({schema: 1, dependencies: {}}));
    await manager.restore();
    assert.deepEqual((await manager.read()).lock.packages, {});
    assert.deepEqual(await fs.readdir(path.join(project, '.dynlex/packages')), []);
  } finally { await fs.rm(temporary, {recursive: true, force: true}); }
});
test('local transitive paths resolve from the declaring library rather than the application', async () => {
  const temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'dynlex-relative-dependencies-'));
  try {
    const project = path.join(temporary, 'workspace/app'); await fs.mkdir(project, {recursive: true});
    for (const [name, dependencies] of [['parent', {helper: {path: '../helper'}}], ['helper', {}]]) {
      const directory = path.join(temporary, 'libraries', name); await fs.mkdir(directory, {recursive: true});
      await fs.writeFile(path.join(directory, 'dynlex-package.json'), JSON.stringify({schema: 1, name, dependencies,
        exports: {'main.dl': 'main.dl'}, files: ['main.dl']}));
      await fs.writeFile(path.join(directory, 'main.dl'), name + ' means: 42');
    }
    const manager = nodeManager(project);
    const installed = await manager.add({path: '../../libraries/parent'}, 'parent');
    assert.equal(installed.lock.packages.helper.source.path.replaceAll('\\', '/'), '../../libraries/helper');
    assert.equal(await fs.readFile(path.join(project, '.dynlex/packages/helper/main.dl'), 'utf8'), 'helper means: 42');
    await manager.remove('parent');
    assert.deepEqual(await fs.readdir(path.join(project, '.dynlex/packages')), []);
  } finally { await fs.rm(temporary, {recursive: true, force: true}); }
});
test('an invalid ignore-file target does not leave a partially installed dependency', async () => {
  const temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'dynlex-install-rollback-'));
  try {
    const root = path.join(temporary, 'app'), source = path.join(temporary, 'library');
    await fs.mkdir(root); await fs.mkdir(source);
    await fs.writeFile(path.join(source, 'dynlex-package.json'), JSON.stringify({schema: 1, name: 'example', files: ['main.dl'], exports: {'main.dl': 'main.dl'}}));
    await fs.writeFile(path.join(source, 'main.dl'), 'answer means: 42');
    await fs.mkdir(path.join(root, '.gitignore'));
    const manager = nodeManager(root);
    await assert.rejects(manager.add({path: '../library'}, 'ui'));
    assert.deepEqual((await manager.read()).manifest.dependencies, {});
    await assert.rejects(fs.access(path.join(root, '.dynlex/packages/example')));
  } finally { await fs.rm(temporary, {recursive: true, force: true}); }
});
