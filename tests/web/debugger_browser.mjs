import assert from 'node:assert/strict';
import { command, evaluate, navigate, closeBrowserSession, runtimeExceptions } from './browser_test_driver.mjs';
try {
  await command('Page.enable'); await command('Runtime.enable');
  await navigate('/build/debugger-test.html');
  const result = await evaluate(`import('/tests/web/debugger_session.mjs').then(module => module.verifyDebuggerSession())`);
  assert.deepEqual(result, { output: '2\n9\n', frames: 2, hoverValue: '2', breakpointValue: '9' });
  assert.equal(await evaluate(`import('/tests/web/debugger_session.mjs').then(module => module.verifyDebuggerAggregates())`), true);
  assert.equal(await evaluate(`import('/tests/web/debugger_session.mjs').then(module => module.verifyDebuggerPureCall())`), true);
  assert.deepEqual(runtimeExceptions, []);
  console.log('Browser debugger: real Wasm paused, inspected runtime values, stepped across imported frames, and continued to a breakpoint, and expanded arrays, padded structs, data declarations and pointers.');
} finally { closeBrowserSession(); }
