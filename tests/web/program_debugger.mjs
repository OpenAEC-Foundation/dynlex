import assert from 'node:assert/strict';
import { ProgramDebugger } from '../../src/web/compiler/debugger.js';
import { readCString } from '../../src/web/ide/public/compiler/runtimeFilesystem.js';

const memory = new WebAssembly.Memory({ initial: 1 });
let cursor = 64;
const text = value => {
  const data = new TextEncoder().encode(value + '\0'), pointer = cursor;
  new Uint8Array(memory.buffer, cursor, data.length).set(data); cursor += data.length;
  return pointer;
};
const name = text('main'), helper = text('helper');
const types = [{ kind: 'integer', name: 'a 64 bit integer', size: 8, bits: 64, signed: true },
  { kind: 'array', name: 'array', size: 16, count: 2, stride: 8, element: 0 }];
const metadata = line => text(JSON.stringify({ uri: 'file:///workspace/main.dl',
  range: { start: { line, character: 0 }, end: { line, character: 5 } },
  types, variables: [{ name: 'counter', type: 0, initialized: true,
    uri: 'file:///workspace/main.dl', range: { start: { line: 0, character: 4 }, end: { line: 0, character: 11 } } },
  { name: 'values', type: 1, initialized: true }] }));
const points = [0, 1, 2, 3].map(metadata);
const view = new DataView(memory.buffer), addresses = 16384, values = 16400;
view.setUint32(addresses, values, true); view.setUint32(addresses + 4, values, true);
view.setBigInt64(values, -9007199254740993n, true); view.setBigInt64(values + 8, 42n, true);
const events = [];
let flushed = 0;
const debuggerSession = new ProgramDebugger(memory, readCString, event => events.push(event), () => flushed++);
debuggerSession.enter(name, points[0]);
let paused = debuggerSession.point(points[0], addresses);
assert.equal(events.at(-1).type, 'program-paused');
assert.equal(events.at(-1).frames[0].variables[0].value, '-9007199254740993');
const reference = events.at(-1).frames[0].variables[1].reference;
assert.deepEqual(debuggerSession.command({ command: 'variables', pause: 1, reference }).map(variable => variable.value),
  ['-9007199254740993', '42']);
debuggerSession.command({ command: 'over', pause: 1 }); await paused;
assert.throws(() => debuggerSession.variables(reference), /no longer paused/);
debuggerSession.enter(helper, points[1]);
assert.equal(debuggerSession.point(points[1], addresses), undefined, 'Step over entered a callee');
debuggerSession.leave();
paused = debuggerSession.point(points[2], addresses);
assert.equal(events.at(-1).pause, 2);
assert.throws(() => debuggerSession.command({ command: 'continue', pause: 1 }), /obsolete pause/);
debuggerSession.command({ command: 'into', pause: 2 }); await paused;
debuggerSession.enter(helper, points[1]);
paused = debuggerSession.point(points[1], addresses);
assert.deepEqual(events.at(-1).frames.map(frame => frame.name), ['helper', 'main']);
debuggerSession.command({ command: 'out', pause: 3 }); await paused;
assert.equal(debuggerSession.point(points[2], addresses), undefined, 'Step out stopped inside its callee');
debuggerSession.leave();
paused = debuggerSession.point(points[3], addresses);
debuggerSession.command({ command: 'continue', pause: 4 }); await paused;
debuggerSession.command({ command: 'breakpoints', breakpoints: [{ uri: 'file:///workspace/main.dl', line: 1 }] });
paused = debuggerSession.point(points[1], addresses);
assert.equal(events.at(-1).reason, 'breakpoint');
debuggerSession.command({ command: 'continue', pause: 5 }); await paused;
debuggerSession.command({ command: 'pause' });
paused = debuggerSession.point(points[2], addresses);
debuggerSession.command({ command: 'continue', pause: 6 }); await paused;
debuggerSession.leave();
assert.equal(flushed, 6, 'Output was not flushed before each pause');
view.setUint32(values, 0x12ffffff, true);
const narrowTypes = [{ kind: 'integer', name: 'a 24-bit signed integer', size: 4, bits: 24, signed: true },
  { kind: 'integer', name: 'a 24-bit unsigned integer', size: 4, bits: 24, signed: false }];
assert.equal(debuggerSession.describe('signed', 0, values, narrowTypes).value, '-1');
assert.equal(debuggerSession.describe('unsigned', 1, values, narrowTypes).value, '16777215');
console.log('Runtime debugger: exact values, child inspection, call stacks, step modes, breakpoints and stale-pause rejection passed.');
