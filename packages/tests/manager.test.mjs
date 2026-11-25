import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {bytes, canonical, documentationUrl, readArchive, verifyPackage} from '../archive.mjs';
import {createPackageManager, emptyProject, packageFromFolder, projectFiles, sourceFromUrl} from '../manager.mjs';
import {nodeHost, nodeManager} from '../node.mjs';

test('manual library URLs identify archives by their path and preserve query parameters', () => {
  assert.deepEqual(sourceFromUrl('https://example.com/ui.zip?token=abc'), {url: 'https://example.com/ui.zip?token=abc'});
  assert.deepEqual(sourceFromUrl('https://github.com/example/ui.git'), {git: 'https://github.com/example/ui.git'});
  assert.throws(() => sourceFromUrl('file:///private/ui.zip'), /HTTP or HTTPS/);
});

async function library(name, dependencies = {}, contents = '42') {
  return packageFromFolder(new Map([
    ['dynlex-package.json', bytes(JSON.stringify({schema: 1, name, exports: {'*': 'lib/*'}, dependencies, files: ['lib', 'assets']}))],
    ['lib/main.dl', bytes(contents)], ['assets/font.bin', new Uint8Array([0, 255, 127])]
  ]), 'revision');
}
test('installation pins a library and its dependencies, mounts imports, and prunes unused packages', async () => {
  const leaf = await library('leaf'), parent = await library('parent', {helper: {catalog: 'leaf'}});
  const catalog = {schema: 1, libraries: [{name: 'leaf', source: {url: 'leaf.zip'}}]};
  const manager = createPackageManager({fetch: async () => Response.json(catalog),
    resolve: async source => source.url.endsWith('leaf.zip') ? leaf : parent}, 'https://example.com/index.json');
  const original = emptyProject();
  const installed = await manager.install(original, {url: 'parent.zip'}, 'ui');
  assert.equal(Object.keys(original.manifest.dependencies).length, 0);
  assert.deepEqual(installed.project.lock.packages.parent.dependencies, {helper: 'leaf'});
  assert.equal(installed.project.lock.packages.leaf.root, '.dynlex/packages/leaf');
  const files = projectFiles(installed.project, installed.packages);
  assert.equal(files['.dynlex/packages/parent/lib/main.dl'], '42');
  assert(files['dynlex.json']); assert(files['dynlex.lock']);
  const removed = manager.remove(installed.project, 'ui');
  assert.deepEqual(removed.lock.packages, {});
});
test('conflicts and failed downloads leave the original project intact', async () => {
  let package_ = await library('one');
  const manager = createPackageManager({resolve: async () => package_});
  const installed = await manager.install(emptyProject(), {url: 'one.zip'}, 'ui');
  const snapshot = JSON.stringify(installed.project);
  package_ = await library('one', {}, 'other');
  await assert.rejects(manager.install(installed.project, {url: 'other.zip'}, 'other'), /conflicts/);
  assert.equal(JSON.stringify(installed.project), snapshot);
  await assert.rejects(manager.install(emptyProject(), {}, '../escape'), /lowercase/);
});
test('circular package dependencies report a real error', async () => {
  const package_ = await library('cycle', {self: {url: 'cycle.zip'}});
  const manager = createPackageManager({resolve: async () => package_});
  await assert.rejects(manager.install(emptyProject(), {url: 'cycle.zip'}, 'cycle'), /Circular/);
});
test('SDK validation checks every file and rejects altered contents and path escapes', async () => {
  const package_ = await library('safe');
  await verifyPackage(package_.files);
  const changed = new Map(package_.files); changed.set('lib/main.dl', bytes('changed'));
  await assert.rejects(verifyPackage(changed), /integrity/);
  const alteredManifest = JSON.parse(new TextDecoder().decode(package_.files.get('sdk.json')));
  alteredManifest.exports = {'*': 'assets/*'};
  const altered = new Map(package_.files); altered.set('sdk.json', bytes(JSON.stringify(alteredManifest)));
  await assert.rejects(verifyPackage(altered), /hashed package descriptor/);
  const metadata = JSON.parse(new TextDecoder().decode(package_.files.get('sdk.json')));
  metadata.files['../outside'] = metadata.files['lib/main.dl'];
  const escaped = new Map(package_.files); escaped.set('../outside', bytes('42'));
  escaped.set('sdk.json', bytes(JSON.stringify(metadata)));
  await assert.rejects(verifyPackage(escaped), /Invalid package path/);
});
test('package documentation cannot execute a URL scheme in the editor', () => {
  assert.equal(documentationUrl('https://example.com/readme'), 'https://example.com/readme');
  assert.throws(() => documentationUrl('javascript:alert(1)'), /HTTP or HTTPS/);
  assert.throws(() => documentationUrl('command:workbench.action.openSettings'), /HTTP or HTTPS/);
});
test('repository libraries reject executable Git transport helpers', async () => {
  await assert.rejects(nodeHost('/tmp').resolve({git: 'ext::command'}), /Unsupported Git library URL protocol/);
});
test('content hash canonicalization matches Python for Unicode and numeric filenames', () => {
  const values = {'1': 'one', '10': 'ten', '2': 'two', '\ue000.dl': 'private', '😀.dl': 'astral'};
  const python = execFileSync('python3', ['-c', 'import sys,json; print(json.dumps(json.loads(sys.argv[1]),sort_keys=True,separators=(",",":")))', JSON.stringify(values)], {encoding: 'utf8'}).trim();
  assert.equal(canonical(values), python);
});
test('local package hashes preserve filenames that are JavaScript object properties', async () => {
  const package_ = await packageFromFolder(new Map([
    ['dynlex-package.json', bytes(JSON.stringify({schema: 1, name: 'example', exports: {'main.dl': 'main.dl'},
      files: ['main.dl', '__proto__']}))], ['main.dl', bytes('answer means: 42')], ['__proto__', bytes('asset')]
  ]), 'local');
  assert(Object.hasOwn(package_.manifest.files, '__proto__'));
  await verifyPackage(package_.files);
});
test('ZIP exports produced by Python load in the portable archive reader', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'dynlex-zip-test-'));
  try {
    const archive = path.join(directory, 'library.zip');
    execFileSync('python3', ['-c', 'import sys,zipfile\nwith zipfile.ZipFile(sys.argv[1],"w",compression=zipfile.ZIP_DEFLATED) as z: z.writestr("lib/main.dl","hello")', archive]);
    const files = await readArchive(new Uint8Array(await fs.readFile(archive)));
    assert.equal(new TextDecoder().decode(files.get('lib/main.dl')), 'hello');
    await assert.rejects(readArchive(bytes('bad zip')));
  } finally { await fs.rm(directory, {recursive: true, force: true}); }
});
test('local projects install, update, restore and remove real files with binary assets', async () => {
  const temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'dynlex-package-test-'));
  try {
    const root = path.join(temporary, 'app'), source = path.join(temporary, 'library');
    await fs.mkdir(root); await fs.mkdir(source);
    const package_ = await library('example');
    for (const [name, data] of package_.files) {
      if (name === 'sdk.json') continue;
      const target = path.join(source, name); await fs.mkdir(path.dirname(target), {recursive: true}); await fs.writeFile(target, data);
    }
    const manager = nodeManager(root);
    const initial = await manager.add({path: '../library'}, 'example');
    assert.deepEqual([...await fs.readFile(path.join(root, '.dynlex/packages/example/assets/font.bin'))], [0, 255, 127]);
    assert.equal(initial.lock.packages.example.revision, 'local');
    await fs.rm(path.join(root, '.dynlex/packages'), {recursive: true});
    await manager.restore();
    await fs.writeFile(path.join(source, 'lib/main.dl'), 'updated');
    const updated = await manager.add({path: '../library'}, 'example', true);
    assert.notEqual(updated.lock.packages.example.contentHash, initial.lock.packages.example.contentHash);
    await manager.remove('example');
    assert.deepEqual(await fs.readdir(path.join(root, '.dynlex/packages')), []);
    assert.deepEqual((await manager.read()).manifest.dependencies, {});
  } finally { await fs.rm(temporary, {recursive: true, force: true}); }
});
test('Git dependencies restore the locked commit even after the branch changes', async () => {
  const temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'dynlex-git-package-'));
  try {
    const root = path.join(temporary, 'app'), source = path.join(temporary, 'source');
    await fs.mkdir(root); await fs.mkdir(source);
    const package_ = await library('example');
    for (const [name, data] of package_.files) {
      if (name === 'sdk.json') continue;
      const target = path.join(source, name); await fs.mkdir(path.dirname(target), {recursive: true}); await fs.writeFile(target, data);
    }
    const git = (...args) => execFileSync('git', ['-C', source, ...args], {stdio: 'pipe'});
    git('init', '-b', 'main'); git('add', '.'); git('-c', 'user.name=Test', '-c', 'user.email=test@example.com', 'commit', '-m', 'first');
    const manager = nodeManager(root), initial = await manager.add({git: source}, 'example');
    await fs.writeFile(path.join(source, 'lib/main.dl'), 'later');
    git('add', '.'); git('-c', 'user.name=Test', '-c', 'user.email=test@example.com', 'commit', '-m', 'second');
    await fs.rm(path.join(root, '.dynlex/packages'), {recursive: true});
    await manager.restore();
    assert.equal((await manager.read()).lock.packages.example.revision, initial.lock.packages.example.revision);
    assert.equal(await fs.readFile(path.join(root, '.dynlex/packages/example/lib/main.dl'), 'utf8'), '42');
  } finally { await fs.rm(temporary, {recursive: true, force: true}); }
});
