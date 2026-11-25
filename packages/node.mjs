import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {inflateRaw} from 'node:zlib';
import {bytes, canonical, readArchive, relativePath, verifyPackage} from './archive.mjs';
import {createPackageManager, emptyProject, packageFromFolder, prune, validateProject} from './manager.mjs';

const exec = promisify(execFile);
async function exists(file) {
  try { await fs.access(file); return true; } catch (error) { if (error.code === 'ENOENT') return false; throw error; }
}
async function folder(directory) {
  const metadata = JSON.parse(await fs.readFile(path.join(directory, 'dynlex-package.json'), 'utf8'));
  const files = new Map();
  async function read(name) {
    let target = directory, stat;
    for (const part of name.split('/')) {
      target = path.join(target, part); stat = await fs.lstat(target);
      if (stat.isSymbolicLink()) throw new Error(`Library sources cannot contain symlinks: ${name}`);
    }
    if (stat.isDirectory()) for (const child of await fs.readdir(target)) await read(`${name}/${child}`);
    else if (stat.isFile()) files.set(name, new Uint8Array(await fs.readFile(target)));
  }
  for (const name of ['dynlex-package.json', ...metadata.files]) {
    relativePath(name);
    await read(name);
  }
  return files;
}
export function nodeHost(root, options = {}) {
  const fetch_ = options.fetch || fetch;
  return {fetch: fetch_, dependencySource(source, parent) {
    if (source.path) {
      if (!parent.path) throw new Error('Published libraries must use catalog, repository or archive dependencies instead of local paths');
      return {...source, path: path.relative(root, path.resolve(root, parent.path, source.path))};
    }
    if (source.url) return {...source, url: new URL(source.url, parent.url).href};
    return source;
  }, async resolve(source) {
    if (source.path) {
      const directory = path.resolve(root, source.path);
      return {...await packageFromFolder(await folder(directory), 'local'), source: {path: source.path}};
    }
    if (source.git) {
      if (!path.isAbsolute(source.git)) {
        const protocol = new URL(source.git).protocol;
        if (!['https:', 'http:', 'ssh:', 'git:', 'file:'].includes(protocol)) throw new Error('Unsupported Git library URL protocol');
      }
      if (source.ref?.startsWith('-')) throw new Error('Invalid Git library revision');
      const temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'dynlex-library-'));
      try {
        const checkout = path.join(temporary, 'source');
        await exec('git', ['clone', '--quiet', '--no-checkout', '--', source.git, checkout], {maxBuffer: 8 * 1024 * 1024});
        const revision = (await exec('git', ['rev-parse', '--verify', `${source.ref || 'HEAD'}^{commit}`], {cwd: checkout})).stdout.trim();
        await exec('git', ['checkout', '--quiet', '--detach', revision], {cwd: checkout});
        return {...await packageFromFolder(await folder(checkout), revision), source: {git: source.git, ref: revision}};
      } finally { await fs.rm(temporary, {recursive: true, force: true}); }
    }
    if (!source.url) throw new Error('Choose a library archive, Git repository or local folder');
    const response = await fetch_(source.url);
    if (!response.ok) throw new Error(`Library download: HTTP ${response.status}`);
    const package_ = await verifyPackage(await readArchive(new Uint8Array(await response.arrayBuffer()), promisify(inflateRaw)));
    if (source.contentHash && package_.manifest.contentHash !== source.contentHash) throw new Error('Library does not match its pinned content hash');
    return {...package_, source: {...source, contentHash: package_.manifest.contentHash}};
  }};
}
export async function readProject(root, allowUnlocked = false) {
  const manifestPath = path.join(root, 'dynlex.json'), lockPath = path.join(root, 'dynlex.lock');
  if (!await exists(manifestPath)) return emptyProject();
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  if (!await exists(lockPath)) {
    if (!allowUnlocked && Object.keys(manifest.dependencies).length) throw new Error('Project dependencies need to be restored');
    return {...emptyProject(), manifest};
  }
  const project = {manifest, lock: JSON.parse(await fs.readFile(lockPath, 'utf8'))};
  return allowUnlocked ? project : validateProject(project);
}
export async function installedPackages(root, project) {
  const packages = new Map();
  for (const [name, entry] of Object.entries(project.lock.packages)) {
    const manifest = JSON.parse(await fs.readFile(path.join(root, entry.root, 'sdk.json'), 'utf8'));
    const files = new Map([['sdk.json', bytes(JSON.stringify(manifest))]]);
    for (const name of Object.keys(manifest.files)) {
      relativePath(name); files.set(name, new Uint8Array(await fs.readFile(path.join(root, entry.root, name))));
    }
    const package_ = await verifyPackage(files);
    if (package_.manifest.package !== name || package_.manifest.contentHash !== entry.contentHash) throw new Error(`Installed library ${name} differs from its lock`);
    packages.set(name, package_);
  }
  return packages;
}
export async function writeProject(root, project, packages) {
  validateProject(project);
  const metadata = [{name: 'dynlex.json', data: bytes(JSON.stringify(project.manifest, null, 2) + '\n')},
    {name: 'dynlex.lock', data: bytes(JSON.stringify(project.lock, null, 2) + '\n')}];
  const ignorePath = path.join(root, '.gitignore');
  const ignore = await exists(ignorePath) ? await fs.readFile(ignorePath) : Buffer.alloc(0);
  if (!ignore.toString('utf8').split(/\r?\n/).includes('/.dynlex/')) metadata.push({name: '.gitignore',
    data: Buffer.concat([ignore, Buffer.from(`${ignore.length && ignore.at(-1) !== 10 ? '\n' : ''}/.dynlex/\n`)])});
  for (const entry of metadata) {
    const filename = path.join(root, entry.name);
    entry.original = await exists(filename) ? {data: await fs.readFile(filename), mode: (await fs.stat(filename)).mode & 0o777} : null;
  }
  const state = path.join(root, '.dynlex'); await fs.mkdir(state, {recursive: true});
  const stage = await fs.mkdtemp(path.join(state, 'install-'));
  const destination = path.join(state, 'packages'), previous = path.join(stage, 'previous');
  let replaced = false, hadPrevious = false;
  try {
    const next = path.join(stage, 'packages'); await fs.mkdir(next);
    for (const [name, entry] of Object.entries(project.lock.packages)) {
      const package_ = packages.get(name);
      if (!package_ || package_.manifest.package !== name || package_.manifest.contentHash !== entry.contentHash) throw new Error(`Library ${name} was not prepared`);
      for (const [file, data] of package_.files) {
        const target = path.join(next, name, file); await fs.mkdir(path.dirname(target), {recursive: true}); await fs.writeFile(target, data);
      }
    }
    for (const entry of metadata) {
      const filename = path.join(stage, entry.name);
      await fs.writeFile(filename, entry.data);
      if (entry.original) await fs.chmod(filename, entry.original.mode);
    }
    if (await exists(destination)) { await fs.rename(destination, previous); hadPrevious = true; }
    await fs.rename(next, destination); replaced = true;
    let committed = 0;
    try {
      for (const entry of metadata) {
        await fs.rename(path.join(stage, entry.name), path.join(root, entry.name)); committed++;
      }
    } catch (error) {
      for (const entry of metadata.slice(0, committed).reverse()) {
        if (entry.original) await fs.writeFile(path.join(root, entry.name), entry.original.data);
        else await fs.rm(path.join(root, entry.name), {force: true});
      }
      throw error;
    }
  } catch (error) {
    if (replaced) await fs.rm(destination, {recursive: true, force: true});
    if (hadPrevious) await fs.rename(previous, destination);
    throw error;
  } finally { await fs.rm(stage, {recursive: true, force: true}); }
}
export function nodeManager(root, catalogUrl, options = {}) {
  const host = nodeHost(root, options), manager = createPackageManager(host, catalogUrl);
  let operation = Promise.resolve();
  function serialize(action) {
    const current = operation.then(action); operation = current.catch(() => {}); return current;
  }
  return {...manager, read: () => readProject(root),
    add: (source, alias, update = false) => serialize(async () => {
      const before = await readProject(root);
      const result = await manager.install(before, source, alias, {update});
      const unchanged = {...result.project, lock: {...result.project.lock,
        packages: Object.fromEntries(Object.entries(result.project.lock.packages).filter(([name]) => !result.packages.has(name)))}};
      const installed = await installedPackages(root, unchanged);
      for (const [name, package_] of result.packages) installed.set(name, package_);
      await writeProject(root, result.project, installed); return result.project;
    }),
    remove: alias => serialize(async () => {
      const before = await readProject(root), next = manager.remove(before, alias);
      const packages = await installedPackages(root, next);
      await writeProject(root, next, packages); return next;
    }),
    restore: () => serialize(async () => {
      const requested = await readProject(root, true);
      validateProject({manifest: {schema: requested.lock.schema, dependencies: requested.lock.requests}, lock: requested.lock});
      let project = emptyProject();
      project.manifest = {...requested.manifest, dependencies: {}};
      project.lock.packages = structuredClone(requested.lock.packages);
      for (const [alias, source] of Object.entries(requested.manifest.dependencies)) {
        if (requested.lock.requests[alias] && canonical(requested.lock.requests[alias]) === canonical(source)) {
          project.manifest.dependencies[alias] = source;
          project.lock.dependencies[alias] = requested.lock.dependencies[alias];
          project.lock.requests[alias] = source;
        }
      }
      prune(project);
      const prepared = new Map();
      for (const [alias, source] of Object.entries(requested.manifest.dependencies)) {
        if (!Object.hasOwn(project.manifest.dependencies, alias)) {
          const result = await manager.install(project, source, alias, {update: true});
          project = result.project;
          for (const [name, package_] of result.packages) prepared.set(name, package_);
        }
      }
      const packages = new Map();
      for (const [name, entry] of Object.entries(project.lock.packages)) {
        const package_ = prepared.get(name) || await host.resolve(entry.source);
        if (package_.manifest.package !== name || package_.manifest.contentHash !== entry.contentHash) throw new Error(`Restored library ${name} differs from its lock`);
        packages.set(name, package_);
      }
      await writeProject(root, project, packages); return project;
    })};
}
