import assert from 'node:assert/strict';
import {navigate,evaluate,captureScreenshot,closeBrowserSession,consoleErrors,runtimeExceptions} from './browser_test_driver.mjs';
try {
  await navigate('/');
  const result=await evaluate(`import('/rendering.mjs').then(module=>module.verifyFarmRenderer())`);
  assert.deepEqual(result.backends,['webgpu','webgl']);
  console.log('Farm scene comparison:',JSON.stringify(result.scene));
  assert.deepEqual(consoleErrors,[]);assert.deepEqual(runtimeExceptions,[]);
  await captureScreenshot('farm-dynlex-scene');
  console.log('Farm HDR sky, filtered cube reflections, cow coat, water normals and fog preserve reference pixels on WebGPU and WebGL.');
} catch(error) {console.error('Farm renderer console:',JSON.stringify(consoleErrors));throw error;}
finally {closeBrowserSession();}
