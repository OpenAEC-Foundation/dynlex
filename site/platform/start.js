import { loadUiApplication } from '../packages/dynlex-ui/platform/browser/application.js';
import { createAudioServices } from '../packages/dynlex-ui/platform/browser/audio.js';
import { createWebServices } from '../packages/dynlex-ui/platform/browser/web-services.js';
import { createCompilerServices } from '../compiler/services.js';
import { createFirebaseServices } from './firebase.js';

export let application;
const root = document.querySelector('main');
const compiler = createCompilerServices(new URL('../compiler/', import.meta.url), new URL('../sdk-sources.json', import.meta.url));
const audio = createAudioServices(new URL('../', import.meta.url));
const web = createWebServices(new URL('../', import.meta.url));
const firebase = createFirebaseServices();
try {
  application = await loadUiApplication(root, new URL('../application.json', import.meta.url), {
    services: { ...web, ...compiler.services, ...audio.services, ...firebase.services },
    synchronousServices: {storage:web.storage,clock:web.clock}
  });
  addEventListener('pagehide', () => {
    application.close(); compiler.close();
    void audio.close().catch(error => console.error('Audio shutdown:', error));
    void firebase.close().catch(error => console.error('Firebase shutdown:', error));
  }, { once: true });
} catch (error) {
  compiler.close();
  void audio.close().catch(error => console.error('Audio shutdown:', error));
  void firebase.close().catch(error => console.error('Firebase shutdown:', error));
  console.error('DynLex site:', error);
  root.textContent = 'An error occurred. Check the log.';
}
