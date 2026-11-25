#!/usr/bin/env node
import {nodeManager} from './node.mjs';
import path from 'node:path';
import {sourceFromUrl} from './manager.mjs';
const [action, argument, ...options] = process.argv.slice(2);
const manager = nodeManager(process.cwd(), process.env.DYNLEX_LIBRARY_CATALOG);
try {
  if (action === 'list') console.log(JSON.stringify(await manager.read(), null, 2));
  else if (action === 'catalog') console.log(JSON.stringify(await manager.catalog(), null, 2));
  else if (action === 'remove') await manager.remove(argument);
  else if (action === 'restore') await manager.restore();
  else if (action === 'update') {
    const project = await manager.read();
    await manager.add(project.manifest.dependencies[argument], argument, true);
  } else if (action === 'add') {
    if (!argument) throw new Error('Choose a library catalog name, URL or local path');
    const alias = options[options.indexOf('--as') + 1];
    if (!options.includes('--as') || !alias) throw new Error('Choose an import name with --as <name>');
    const source = argument.startsWith('.') || path.isAbsolute(argument) ? {path: argument}
      : argument.includes('://') ? sourceFromUrl(argument) : {catalog: argument};
    await manager.add(source, alias);
  } else throw new Error('Usage: libraries <add source --as name | remove name | update name | restore | list | catalog>');
} catch (error) { console.error(error.message); process.exitCode = 1; }
