import assert from 'node:assert/strict';
import {navigate,evaluate,closeBrowserSession,consoleErrors,runtimeExceptions} from './browser_test_driver.mjs';
try {
  await navigate('/');
  assert.deepEqual(await evaluate(`import('/rendering.mjs').then(module=>module.verifyFarmRenderer())`),['webgpu','webgl']);
  assert.deepEqual(consoleErrors,[]);assert.deepEqual(runtimeExceptions,[]);
  console.log('Farm HDR sky and filtered cube reflections preserve original pixels and DynLex CPU samples on WebGPU and WebGL.');
} catch(error) {console.error('Farm renderer console:',JSON.stringify(consoleErrors));throw error;}
finally {closeBrowserSession();}
