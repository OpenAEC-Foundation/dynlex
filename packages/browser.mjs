import {readArchive, verifyPackage} from './archive.mjs';
import {createPackageManager, emptyProject, packageFromFolder, projectFiles, sourceFromUrl, validateProject} from './manager.mjs';

function database() {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open('dynlex-libraries', 1);
    request.onupgradeneeded = () => request.result.createObjectStore('packages');
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}
async function stored(database_, key, value) {
  const db = await database_;
  return new Promise((resolve, reject) => {
    const transaction = db.transaction('packages', value ? 'readwrite' : 'readonly');
    const store = transaction.objectStore('packages');
    const request = value ? store.put(value, key) : store.get(key);
    transaction.oncomplete = () => resolve(request.result);
    transaction.onerror = () => reject(transaction.error);
    transaction.onabort = () => reject(transaction.error);
  });
}
async function github(source, fetch_) {
  const url = new URL(source.git);
  if (url.hostname !== 'github.com') throw new Error('Browser Git libraries require a GitHub repository; use a source SDK archive for other hosts');
  const parts = url.pathname.replace(/\.git$/, '').split('/').filter(Boolean);
  if (parts.length !== 2) throw new Error('Enter the GitHub repository URL');
  const api = `https://api.github.com/repos/${parts.join('/')}`;
  const json = async path => {
    const response = await fetch_(api + path);
    if (!response.ok) throw new Error(`GitHub library: HTTP ${response.status}`);
    return response.json();
  };
  const commit = await json('/commits/' + encodeURIComponent(source.ref || 'HEAD'));
  const revision = commit.sha;
  const raw = `https://raw.githubusercontent.com/${parts.join('/')}/${revision}/`;
  const metadataResponse = await fetch_(raw + 'dynlex-package.json');
  if (!metadataResponse.ok) throw new Error('The Git repository has no dynlex-package.json');
  const metadataText = await metadataResponse.text(), metadata = JSON.parse(metadataText);
  const tree = await json(`/git/trees/${commit.commit.tree.sha}?recursive=1`);
  if (tree.truncated) throw new Error('The GitHub library tree is incomplete');
  const files = new Map([['dynlex-package.json', new TextEncoder().encode(metadataText)]]);
  for (const item of tree.tree) {
    if (item.type !== 'blob' || !metadata.files.some(prefix => item.path === prefix || item.path.startsWith(prefix + '/'))) continue;
    if (item.mode === '120000') throw new Error('Library sources cannot contain symlinks');
    const response = await fetch_(raw + item.path.split('/').map(encodeURIComponent).join('/'));
    if (!response.ok) throw new Error(`Library file: HTTP ${response.status}`);
    files.set(item.path, new Uint8Array(await response.arrayBuffer()));
  }
  return {...await packageFromFolder(files, revision), source: {git: source.git, ref: revision}};
}
function selectFolder() {
  return new Promise((resolve, reject) => {
    const input = document.createElement('input');
    input.type = 'file'; input.webkitdirectory = true;
    input.oncancel = () => resolve(null);
    input.onchange = async () => {
      try {
        const files = new Map();
        const selected = [...input.files];
        const relative = file => file.webkitRelativePath.split('/').slice(1).join('/');
        const descriptor = selected.find(file => relative(file) === 'dynlex-package.json');
        if (!descriptor) throw new Error('The selected folder has no dynlex-package.json');
        const metadata = JSON.parse(await descriptor.text());
        for (const file of input.files) {
          const path = relative(file);
          if (path !== 'dynlex-package.json' && !metadata.files.some(prefix => path === prefix || path.startsWith(prefix + '/'))) continue;
          files.set(path, new Uint8Array(await file.arrayBuffer()));
        }
        resolve(files);
      } catch (error) { reject(error); }
    };
    input.click();
  });
}
export function createLibraryServices(catalogUrl, options = {}) {
  let db;
  const storage = options.storage || ((key, value) => stored(db ??= database(), key, value));
  const fetch_ = (options.fetch || globalThis.fetch).bind(globalThis);
  const host = {fetch: fetch_, dependencySource(source, parent) {
    if (source.path) throw new Error('Browser libraries require portable catalog, repository or archive dependencies instead of local paths');
    if (source.url) return {...source, url: new URL(source.url, parent.url).href};
    return source;
  }, async resolve(source) {
    if (source.contentHash) {
      const cached = await storage(source.contentHash);
      if (cached) return {...await verifyPackage(cached.files), source};
    }
    let package_;
    if (source.local) {
      const cached = await storage(source.local);
      if (!cached) throw new Error('The local library is no longer available; add its folder again');
      package_ = {...await verifyPackage(cached.files), source};
    } else if (source.git) package_ = await github(source, fetch_);
    else {
      const response = await fetch_(source.url);
      if (!response.ok) throw new Error(`Library download: HTTP ${response.status}`);
      package_ = {...await verifyPackage(await readArchive(new Uint8Array(await response.arrayBuffer()))), source};
    }
    if (source.contentHash && package_.manifest.contentHash !== source.contentHash) throw new Error('Library content hash does not match the dependency lock');
    package_.source = {...(package_.source || source), contentHash: package_.manifest.contentHash};
    await storage(package_.manifest.contentHash, {files: package_.files});
    return package_;
  }};
  const manager = createPackageManager(host, catalogUrl);
  async function materialize(project, packages = new Map()) {
    for (const [name, entry] of Object.entries(project.lock.packages)) {
      if (!packages.has(name)) packages.set(name, await host.resolve({...entry.source, contentHash: entry.contentHash}));
    }
    return {project, sources: projectFiles(project, packages)};
  }
  return {async runtimeFiles(project) {
    validateProject(project);
    const files = {};
    for (const [name, entry] of Object.entries(project.lock.packages)) {
      const package_ = await host.resolve({...entry.source, contentHash: entry.contentHash});
      if (package_.manifest.package !== name) throw new Error('The cached library identity differs from its lock');
      for (const [path, data] of package_.files) files[`${entry.root}/${path}`] = data;
    }
    return files;
  }, async libraries(payload, {signal}) {
    signal.throwIfAborted();
    if (payload.operation === 'catalog') return {libraries: await manager.catalog()};
    const project = payload.project || emptyProject();
    validateProject(project);
    if (payload.operation === 'documentation') {
      const name = project.lock.dependencies[payload.alias];
      const url = project.lock.packages[name].documentation;
      if (!url) throw new Error('This library does not declare a documentation URL');
      return {documentation: url};
    }
    if (payload.operation === 'restore') return materialize(project);
    if (payload.operation === 'remove') return materialize(manager.remove(project, payload.alias));
    let source = payload.url === undefined ? payload.source : sourceFromUrl(payload.url);
    if (payload.operation === 'folder' || (payload.operation === 'update' && project.manifest.dependencies[payload.alias]?.local)) {
      const files = await (options.selectFolder || selectFolder)();
      if (!files) return {cancelled: true};
      const package_ = await packageFromFolder(files, 'local');
      await storage(package_.manifest.contentHash, {files: package_.files});
      source = {local: package_.manifest.contentHash};
    } else if (payload.operation === 'update') source = project.manifest.dependencies[payload.alias];
    else if (payload.operation !== 'add') throw new Error(`Unknown library operation: ${payload.operation}`);
    const result = await manager.install(project, source, payload.alias, {update: payload.operation === 'update'});
    signal.throwIfAborted();
    return materialize(result.project, result.packages);
  }};
}
