// The source SDK format is shared with the website's exported library archives.
const decoder = new TextDecoder('utf-8', {fatal: true});
export const text = bytes => decoder.decode(bytes);
export const bytes = value => new TextEncoder().encode(value);
export function relativePath(value) {
  if (typeof value !== 'string' || !value || /[\\\x00-\x1f\x7f:]/.test(value)
      || value.split('/').some(part => !part || part === '.' || part === '..'))
    throw new Error(`Invalid package path: ${value}`);
  return value;
}
export function canonical(value) {
  const compare = (left, right) => {
    const a = Array.from(left, character => character.codePointAt(0));
    const b = Array.from(right, character => character.codePointAt(0));
    for (let index = 0; index < Math.min(a.length, b.length); index++) if (a[index] !== b[index]) return a[index] - b[index];
    return a.length - b.length;
  };
  const encode = item => {
    if (Array.isArray(item)) return '[' + item.map(encode).join(',') + ']';
    if (item && typeof item === 'object') return '{' + Object.keys(item).sort(compare)
      .map(key => JSON.stringify(key) + ':' + encode(item[key])).join(',') + '}';
    return JSON.stringify(item);
  };
  const encoded = encode(value);
  return encoded.replace(/[\u007f-\uffff]/g, character =>
    '\\u' + character.charCodeAt(0).toString(16).padStart(4, '0'));
}
export async function sha256(data) {
  return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', data)),
    byte => byte.toString(16).padStart(2, '0')).join('');
}
async function inflateRaw(data) {
  return new Uint8Array(await new Response(new Blob([data]).stream()
    .pipeThrough(new DecompressionStream('deflate-raw'))).arrayBuffer());
}
export async function readArchive(data, inflate = inflateRaw) {
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  let end = data.length - 22;
  while (end >= Math.max(0, data.length - 65557) && view.getUint32(end, true) !== 0x06054b50) end--;
  if (end < 0 || end < data.length - 65557) throw new Error('Invalid library ZIP directory');
  if (view.getUint16(end + 4, true) || view.getUint16(end + 6, true)) throw new Error('Split ZIP archives are unsupported');
  const count = view.getUint16(end + 10, true), files = new Map();
  let offset = view.getUint32(end + 16, true);
  for (let index = 0; index < count; index++) {
    if (view.getUint32(offset, true) !== 0x02014b50) throw new Error('Invalid library ZIP entry');
    const flags = view.getUint16(offset + 8, true), method = view.getUint16(offset + 10, true);
    const compressed = view.getUint32(offset + 20, true), size = view.getUint32(offset + 24, true);
    const nameLength = view.getUint16(offset + 28, true), extra = view.getUint16(offset + 30, true);
    const comment = view.getUint16(offset + 32, true), local = view.getUint32(offset + 42, true);
    const name = text(data.subarray(offset + 46, offset + 46 + nameLength));
    offset += 46 + nameLength + extra + comment;
    if (name.endsWith('/')) { relativePath(name.slice(0, -1)); continue; }
    relativePath(name);
    if (files.has(name)) throw new Error(`Duplicate library file: ${name}`);
    if (flags & 1) throw new Error('Encrypted libraries are unsupported');
    if (view.getUint32(local, true) !== 0x04034b50) throw new Error('Invalid library ZIP data');
    const start = local + 30 + view.getUint16(local + 26, true) + view.getUint16(local + 28, true);
    if (start + compressed > data.length) throw new Error('Truncated library ZIP data');
    let contents = data.slice(start, start + compressed);
    if (method === 8) contents = new Uint8Array(await inflate(contents));
    else if (method !== 0) throw new Error(`Unsupported library ZIP compression: ${method}`);
    if (contents.length !== size) throw new Error(`Invalid library file size: ${name}`);
    files.set(name, contents);
  }
  return files;
}
export async function verifyPackage(files) {
  if (!files.has('sdk.json')) throw new Error('The library archive has no sdk.json');
  const manifest = JSON.parse(text(files.get('sdk.json')));
  validatePackageMetadata(manifest);
  if (files.size !== Object.keys(manifest.files).length + 1) throw new Error('Library manifest does not match its files');
  for (const [name, digest] of Object.entries(manifest.files)) {
    relativePath(name);
    if (!files.has(name) || await sha256(files.get(name)) !== digest) throw new Error(`Library integrity check failed: ${name}`);
  }
  if (await sha256(bytes(canonical(manifest.files))) !== manifest.contentHash) throw new Error('Library content hash does not match');
  if (!files.has('dynlex-package.json')) throw new Error('The library archive has no dynlex-package.json');
  const descriptor = JSON.parse(text(files.get('dynlex-package.json')));
  validatePackageMetadata({...descriptor, package: descriptor.name});
  if (canonical(packageMetadata(manifest)) !== canonical(packageMetadata({...descriptor, package: descriptor.name})))
    throw new Error('Library metadata differs from its hashed package descriptor');
  return {manifest, files};
}
export function packageMetadata(manifest) {
  return {schema: manifest.schema, package: manifest.package, exports: manifest.exports,
    dependencies: manifest.dependencies || {}, description: manifest.description || '', documentation: manifest.documentation || '',
    title: manifest.title || manifest.package, publisher: manifest.publisher || '', importName: manifest.importName || manifest.package};
}
export function documentationUrl(value) {
  if (value === undefined || value === '') return '';
  const url = new URL(value);
  if (!['http:', 'https:'].includes(url.protocol)) throw new Error('Library documentation must use HTTP or HTTPS');
  return url.href;
}
export function validatePackageMetadata(manifest) {
  if (manifest.schema !== 1 || typeof manifest.package !== 'string' || !/^[a-z][a-z0-9-]*$/.test(manifest.package)) throw new Error('Invalid library manifest');
  validateExports(manifest.exports);
  documentationUrl(manifest.documentation);
  for (const field of ['description', 'documentation', 'title', 'publisher', 'importName', 'sourceRevision']) {
    if (manifest[field] !== undefined && typeof manifest[field] !== 'string') throw new Error(`Library ${field} must be text`);
  }
  if (manifest.dependencies !== undefined && (!manifest.dependencies || typeof manifest.dependencies !== 'object' || Array.isArray(manifest.dependencies)))
    throw new Error('Library dependencies must be an object');
}
export function validateExports(exports) {
  if (!exports || typeof exports !== 'object' || Array.isArray(exports) || !Object.keys(exports).length)
    throw new Error('A library must declare its public exports');
  for (const [name, target] of Object.entries(exports)) {
    if (typeof target !== 'string') throw new Error('Library export targets must be text');
    relativePath(name.replaceAll('*', '_')); relativePath(target.replaceAll('*', '_'));
    if ((name.match(/\*/g) || []).length > 1 || (name.match(/\*/g) || []).length !== (target.match(/\*/g) || []).length
        || (name.includes('*') && (!name.endsWith('*') || !target.endsWith('*'))))
      throw new Error('Library export wildcards must be matching trailing stars');
  }
}
