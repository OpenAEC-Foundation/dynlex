import { loadUiApplication } from '../packages/dynlex-ui/platform/browser/application.js';
import { createWebServices } from '../packages/dynlex-ui/platform/browser/web-services.js';
import { createCompilerServices } from '../compiler/services.js';

export let application;
const root = document.querySelector('main');
const compiler = createCompilerServices(new URL('../compiler/', import.meta.url), new URL('../sdk-sources.json', import.meta.url));
try {
  application = await loadUiApplication(root, new URL('../application.json', import.meta.url), {
    services: { ...createWebServices(new URL('../', import.meta.url)), ...compiler.services }
  });
  addEventListener('pagehide', () => { application.close(); compiler.close(); }, { once: true });
} catch (error) {
  compiler.close();
  console.error('DynLex site:', error);
  root.textContent = 'An error occurred. Check the log.';
}
