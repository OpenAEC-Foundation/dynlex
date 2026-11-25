import {bytes, canonical, documentationUrl, relativePath, sha256, text, validateExports, validatePackageMetadata} from './archive.mjs';

export const defaultCatalog = 'https://dynlex.com/libraries/index.json';
export function sourceFromUrl(value) {
  const url = new URL(value);
  if (!['http:', 'https:'].includes(url.protocol)) throw new Error('Enter an HTTP or HTTPS library URL');
  return url.pathname.endsWith('.zip') ? {url: url.href} : {git: url.href};
}
export const emptyProject = () => ({manifest: {schema: 1, dependencies: {}},
  lock: {schema: 1, dependencies: {}, requests: {}, packages: {}}});
export function aliasName(value) {
  if (typeof value !== 'string' || !/^[a-z][a-z0-9-]*$/.test(value) || ['lib', 'packages', 'site'].includes(value))
    throw new Error('Use a lowercase library name containing letters, digits or hyphens');
  return value;
}
export function validateProject(project) {
  if (project.manifest.schema !== 1 || project.lock.schema !== 1) throw new Error('Unsupported DynLex project schema');
  for (const [label, value] of [['dependencies', project.manifest.dependencies], ['locked dependencies', project.lock.dependencies],
    ['locked requests', project.lock.requests], ['locked packages', project.lock.packages]]) {
    if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error(`Project ${label} must be an object`);
  }
  for (const [alias, source] of Object.entries(project.manifest.dependencies)) {
    aliasName(alias);
    if (!source || typeof source !== 'object') throw new Error(`Invalid dependency: ${alias}`);
    const name = project.lock.dependencies[alias], entry = Object.hasOwn(project.lock.packages, name) ? project.lock.packages[name] : undefined;
    if (!entry || !Object.hasOwn(project.lock.requests, alias) || canonical(project.lock.requests[alias]) !== canonical(source))
      throw new Error(`Dependency ${alias} needs to be restored`);
  }
  if (Object.keys(project.manifest.dependencies).length !== Object.keys(project.lock.dependencies).length)
    throw new Error('The dependency lock does not match this project');
  for (const [name, entry] of Object.entries(project.lock.packages)) {
    if (!/^[a-z][a-z0-9-]*$/.test(name) || entry.root !== `.dynlex/packages/${name}` || typeof entry.revision !== 'string'
        || !/^[a-f0-9]{64}$/.test(entry.contentHash)) throw new Error('Invalid locked library');
    validateExports(entry.exports);
    documentationUrl(entry.documentation);
    for (const dependency of Object.values(entry.dependencies)) {
      if (!project.lock.packages[dependency]) throw new Error(`Missing transitive library: ${dependency}`);
    }
  }
  return project;
}
export function projectFiles(project, packages) {
  validateProject(project);
  const result = {'dynlex.json': JSON.stringify(project.manifest, null, 2) + '\n',
    'dynlex.lock': JSON.stringify(project.lock, null, 2) + '\n'};
  for (const [name, entry] of Object.entries(project.lock.packages)) {
    const package_ = packages.get(name);
    if (!package_ || package_.manifest.package !== name || package_.manifest.contentHash !== entry.contentHash) throw new Error(`Library ${name} is not installed`);
    for (const [path, data] of package_.files) {
      if (path.endsWith('.dl')) result[`${entry.root}/${path}`] = text(data);
    }
  }
  return result;
}
export function prune(project) {
  const used = new Set();
  function visit(name) {
    if (used.has(name)) return;
    used.add(name);
    for (const child of Object.values(project.lock.packages[name].dependencies)) visit(child);
  }
  Object.values(project.lock.dependencies).forEach(visit);
  for (const name of Object.keys(project.lock.packages)) if (!used.has(name)) delete project.lock.packages[name];
  return validateProject(project);
}
export async function packageFromFolder(files, revision) {
  if (!files.has('dynlex-package.json')) throw new Error('The library folder has no dynlex-package.json');
  const metadata = JSON.parse(text(files.get('dynlex-package.json')));
  validatePackageMetadata({...metadata, package: metadata.name});
  const selected = new Map([...files].filter(([name]) => name === 'dynlex-package.json'
    || metadata.files.some(prefix => name === prefix || name.startsWith(prefix + '/'))));
  const digests = Object.create(null);
  for (const [name, data] of [...selected].sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0)) {
    relativePath(name); digests[name] = await sha256(data);
  }
  const manifest = {schema: 1, package: metadata.name, exports: metadata.exports,
    dependencies: metadata.dependencies || {}, description: metadata.description || '',
    documentation: metadata.documentation || '', title: metadata.title || metadata.name,
    publisher: metadata.publisher || '', importName: metadata.importName || metadata.name,
    sourceRevision: revision, files: digests, contentHash: await sha256(bytes(canonical(digests)))};
  selected.set('sdk.json', bytes(JSON.stringify(manifest)));
  return {manifest, files: selected};
}
export function createPackageManager(host, catalogUrl = defaultCatalog) {
  async function catalog() {
    const response = await host.fetch(catalogUrl);
    if (!response.ok) throw new Error(`Library catalog: HTTP ${response.status}`);
    const data = await response.json();
    if (data.schema !== 1 || !Array.isArray(data.libraries)) throw new Error('Invalid library catalog');
    return data.libraries.map(item => {
      if (typeof item.name !== 'string' || !/^[a-z][a-z0-9-]*$/.test(item.name)) throw new Error('Invalid catalog library name');
      const normalized = {...item, alias: item.alias === undefined ? item.name : item.alias,
        title: item.title === undefined ? item.name : item.title,
        publisher: item.publisher === undefined ? '' : item.publisher,
        description: item.description === undefined ? '' : item.description};
      aliasName(normalized.alias);
      for (const field of ['title', 'publisher', 'description']) if (typeof normalized[field] !== 'string') throw new Error(`Catalog ${field} must be text`);
      if (!item.source || typeof item.source !== 'object' || Boolean(item.source.url) === Boolean(item.source.git))
        throw new Error('Catalog libraries must declare an archive or Git repository');
      normalized.source = {...item.source, ...(item.source.url ? {url: new URL(item.source.url, catalogUrl).href} : {})};
      return normalized;
    });
  }
  async function install(project, request, alias, options = {}) {
    validateProject(project);
    aliasName(alias);
    if (!options.update && Object.hasOwn(project.manifest.dependencies, alias)) throw new Error(`The project already has a library named ${alias}`);
    const next = structuredClone(project), packages = new Map();
    if (options.update && Object.hasOwn(next.manifest.dependencies, alias)) {
      delete next.manifest.dependencies[alias]; delete next.lock.dependencies[alias]; delete next.lock.requests[alias];
      prune(next);
    }
    const active = new Set();
    async function resolve(source, requestedAlias, parent) {
      const original = source;
      const expectedName = source.catalog;
      if (parent && host.dependencySource) source = host.dependencySource(source, parent);
      if (source.catalog) {
        const item = (await catalog()).find(entry => entry.name === source.catalog);
        if (!item) throw new Error(`Library is absent from the catalog: ${source.catalog}`);
        source = item.source;
      }
      const package_ = await host.resolve(source);
      const {manifest} = package_, name = manifest.package;
      if (expectedName && expectedName !== name) throw new Error('The library archive does not match its catalog identity');
      if (active.has(name)) throw new Error(`Circular library dependency: ${name}`);
      const existing = Object.hasOwn(next.lock.packages, name) ? next.lock.packages[name] : undefined;
      if (existing && existing.contentHash !== manifest.contentHash && (!options.update || canonical(existing.request) !== canonical(original)))
        throw new Error(`Library ${name} conflicts with the installed revision`);
      if (packages.has(name)) {
        if (packages.get(name).manifest.contentHash !== manifest.contentHash) throw new Error(`Conflicting versions of ${name}`);
        return name;
      }
      active.add(name); packages.set(name, package_);
      const dependencies = {};
      for (const [childAlias, childSource] of Object.entries(manifest.dependencies || {}))
        dependencies[aliasName(childAlias)] = await resolve(childSource, childAlias, package_.source || source);
      active.delete(name);
      next.lock.packages[name] = {root: `.dynlex/packages/${name}`, source: package_.source || source,
        request: original, contentHash: manifest.contentHash, revision: manifest.sourceRevision || manifest.contentHash,
        exports: manifest.exports, dependencies, description: manifest.description || '', documentation: documentationUrl(manifest.documentation)};
      return name;
    }
    next.lock.dependencies[alias] = await resolve(request, alias);
    next.manifest.dependencies[alias] = request;
    next.lock.requests[alias] = request;
    prune(next);
    return {project: next, packages};
  }
  function remove(project, alias) {
    if (!Object.hasOwn(project.manifest.dependencies, alias)) throw new Error(`Unknown dependency: ${alias}`);
    const next = structuredClone(project);
    delete next.manifest.dependencies[alias]; delete next.lock.dependencies[alias];
    delete next.lock.requests[alias];
    return prune(next);
  }
  return {catalog, install, remove};
}
