import {CatalogEntry, Project, Source} from './manager.mjs';
export interface NodeManager {
  catalog(): Promise<CatalogEntry[]>;
  read(): Promise<Project>;
  add(source: Source, alias: string, update?: boolean): Promise<Project>;
  remove(alias: string): Promise<Project>;
  restore(): Promise<Project>;
}
export function nodeManager(root: string, catalogUrl?: string): NodeManager;
