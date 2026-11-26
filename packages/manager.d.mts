export interface Source {url?: string; contentHash?: string; git?: string; ref?: string; path?: string; catalog?: string;}
export interface LockedPackage {root: string; source: Source; request: Source; contentHash: string; revision: string;
  exports: Record<string, string>; dependencies: Record<string, string>; description: string; documentation: string;}
export interface Project {manifest: {schema: number; dependencies: Record<string, Source>};
  lock: {schema: number; dependencies: Record<string, string>; requests: Record<string, Source>; packages: Record<string, LockedPackage>};}
export interface CatalogEntry {name: string; alias: string; title: string; description: string; publisher: string; source: Source; documentation: string;}
export const defaultCatalog: string;
export function sourceFromUrl(value: string): Source;
export function emptyProject(): Project;
export function aliasName(value: string): string;
export function validateProject(project: Project): Project;
