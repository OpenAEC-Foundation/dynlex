import { ProgramDebugger } from '../../src/web/compiler/debugger.js';
import { buildRuntimeImports, createRuntimeFilesystem, inspectRuntimeWasmLayout } from '../../src/web/ide/public/compiler/runtimeImports.js';
import { readCString } from '../../src/web/ide/public/compiler/runtimeFilesystem.js';

const require = (condition, message) => { if (!condition) throw new Error(message); };

async function debugProgram(file) {
  const wasm = new Uint8Array(await (await fetch(file)).arrayBuffer());
  const output = [], pauses = [], waiting = [];
  const runtime = buildRuntimeImports(output, createRuntimeFilesystem(), inspectRuntimeWasmLayout(wasm));
  const session = new ProgramDebugger(runtime.env.__linear_memory, readCString, event => {
    if (event.type === 'program-paused') {
      if (waiting.length) waiting.shift()(event); else pauses.push(event);
    }
  }, () => {});
  runtime.env.dynlex_debug_enter = (name, location) => session.enter(name, location);
  runtime.env.dynlex_debug_leave = () => session.leave();
  runtime.env.dynlex_debug_location = new WebAssembly.Suspending((metadata, addresses) => session.point(metadata, addresses));
  const { instance } = await WebAssembly.instantiate(wasm, runtime);
  const running = WebAssembly.promising(instance.exports.main)();
  const pause = () => pauses.length ? Promise.resolve(pauses.shift()) : Promise.race([
    new Promise(resolve => waiting.push(resolve)), running.then(() => { throw new Error('The program finished before its next pause'); })
  ]);
  return { session, running, pause, output };
}

export async function verifyDebuggerSession() {
  const { session, running, pause, output } = await debugProgram('/build/debugger-test.wasm');
  const value = (stopped, name, frame = 0) => stopped.frames[frame].variables.find(variable => variable.name === name)?.value;
  const command = (stopped, name) => session.command({ command: name, pause: stopped.pause });
  let stopped = await pause();
  require(stopped.frames[0].range.start.line === 1, 'Debugging did not stop before the first statement');
  require(value(stopped, 'counter') === '<uninitialized>', 'The debugger read an uninitialized variable');
  command(stopped, 'over'); stopped = await pause();
  require(stopped.frames[0].range.start.line === 2 && value(stopped, 'counter') === '1', 'Step over lost a stored value');
  command(stopped, 'into'); stopped = await pause();
  require(stopped.frames.length === 2 && stopped.frames[0].uri.endsWith('/helper.dl'), 'Step into did not visit the imported function');
  require(value(stopped, 'number') === '1' && value(stopped, 'counter', 1) === '1', 'Call frame arguments or caller locals are wrong');
  command(stopped, 'over'); stopped = await pause();
  require(value(stopped, 'increased') === '2', 'The paused value did not reflect execution');
  command(stopped, 'out'); stopped = await pause();
  require(stopped.frames.length === 1 && stopped.frames[0].range.start.line === 3, 'Step out did not return to the caller');
  require(output.join('') === '2\n', 'Stepping did not produce the callee output');
  const mainUri = stopped.frames[0].uri;
  session.command({ command: 'breakpoints', breakpoints: [{ uri: mainUri, line: 4 }] });
  command(stopped, 'continue'); stopped = await pause();
  require(stopped.reason === 'breakpoint' && value(stopped, 'counter') === '9', 'Continue missed the breakpoint or retained an old value');
  command(stopped, 'continue'); await running;
  require(output.join('') === '2\n9\n' && session.frames.length === 0, 'The debug session lost output or retained call frames');
  return { output: output.join(''), frames: 2, hoverValue: '2', breakpointValue: '9' };
}

export async function verifyDebuggerAggregates() {
  const { session, running, pause } = await debugProgram('/build/debugger-aggregates.wasm');
  let stopped = await pause();
  session.command({ command: 'breakpoints', breakpoints: [{ uri: stopped.frames[0].uri, line: 17 }] });
  session.command({ command: 'continue', pause: stopped.pause });
  stopped = await pause();
  const variables = stopped.frames[0].variables;
  const variable = name => variables.find(value => value.name === name);
  require(variable('narrow').value === '-1', 'A narrow signed integer included storage padding');
  require(variable('fixed answer').value === '42', 'A data declaration lost its runtime storage');
  const fields = session.variables(variable('record').reference);
  require(fields[0].value === '7' && fields[1].value === '9007199254740993', 'Class inspection lost padding or integer precision');
  require(session.variables(variable('items').reference).map(item => item.value).join(',') === '4,5', 'Array inspection used the wrong element stride');
  const target = session.variables(variable('target').reference)[0];
  require(session.variables(target.reference)[1].value === '9007199254740993', 'Pointer inspection did not reach its target');
  session.command({ command: 'continue', pause: stopped.pause }); await running;
  return true;
}

export async function verifyDebuggerPureCall() {
  const { session, running, pause } = await debugProgram('/build/debugger-pure-call.wasm');
  let stopped = await pause();
  session.command({ command: 'into', pause: stopped.pause }); stopped = await pause();
  require(stopped.frames.length === 2 && stopped.frames[0].variables.find(variable => variable.name === 'number')?.value === '7',
    'Step into skipped a pure function called with a literal argument');
  session.command({ command: 'out', pause: stopped.pause }); stopped = await pause();
  require(stopped.frames[0].variables.find(variable => variable.name === 'answer')?.value === '8', 'A pure debug call changed its result');
  session.command({ command: 'continue', pause: stopped.pause }); await running;
  return true;
}
