import assert from "node:assert/strict";
import { mkdtempSync, writeFileSync, rmSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { tmpdir } from "node:os";
import path from "node:path";
import { parseFarmPlan, stepRoutine, newRoutine, FARM_PREFIX } from "../../web/farm-program.js";
import { parseRiverTrace } from "../../web/river-challenge-model.js";
import { InstructionGate, executionOutput } from "../../web/execution-trace.js";

const directory = mkdtempSync(path.join(tmpdir(), "dynlex-execution-trace-test-"));
function run(source) {
  const file = path.join(directory,"main.dl"), binary = path.join(directory,"main.out");
  writeFileSync(file, source);
  execFileSync(path.resolve("build/dynlex"), [file,"--trace-execution","-o",binary]);
  return execFileSync(binary,{encoding:"utf8"});
}
try {
  const unicode = executionOutput(run(`to echo {value} twice:
    @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "int", 32), 1, "%s\\n", value))
echo "🌽é中" twice`), 0);
  assert.equal(unicode[0].text,"🌽é中");
  // Intrinsic output inherits the authored call marker, in UTF-16 editor coordinates.
  assert.equal(unicode[0].range.start.line,2);
  assert.equal(unicode[0].range.end.character,'echo "🌽é中" twice'.length);
  const plan = parseFarmPlan(run(FARM_PREFIX + `to rest:
    wait
loop 2 times:
    rest
if my hands are empty:
    turn left
else:
    turn right`));
  assert.deepEqual(plan.map(instruction => [instruction.op,instruction.range.start.line]), [
    ["loop",2],["action",1],["next",2],["test",4],["action",5],["jump",6],["action",7]
  ]);
  const routine = newRoutine(), actions = [];
  const step = () => stepRoutine(routine, plan, () => false, ({key}) => { actions.push(key); return {done:true,message:key}; });
  step(); assert.deepEqual(actions, [], "Stepping a loop header does not execute its body");
  step(); step(); step(); step(); step();
  assert.equal(routine.trace.branch, false);
  assert.deepEqual(actions,["wait","wait"]);
  step(); assert.deepEqual(actions,["wait","wait","turn"]);
  assert.equal(plan[6].argument,3);

  const source = `import lib/river_challenge.dl

to take a passenger:
    get the sheep in the boat
if 1 = 2:
    get the hay in the boat
else:
    take a passenger
row to the other side`;
  const trace = parseRiverTrace(run(source));
  assert.deepEqual(trace.commands.map(command => [command.action,command.range.start.line,command.branch]), [
    ["TEST",2,false],["LOAD",1,null],["CROSS",6,null]
  ]);
  assert.equal(trace.outcome,"running");
  const dynamic = executionOutput(run(`import lib/std.dl

set counter to 0
loop 2 times:
    if counter = 0:
        print "first" as a line
    else:
        print "second" as a line
    increment counter`));
  assert.deepEqual(dynamic.filter(record => record.branch !== null).map(record => [record.range.start.line,record.branch]), [[2,true],[2,false]]);
  assert.throws(() => executionOutput("DYNLEX|TRACE|0|0|0|2|-1"), /range/);
} finally { rmSync(directory,{recursive:true,force:true}); }

const gate = new InstructionGate(), abort = new AbortController();
gate.pause(); let steps = 0;
const work = (async () => { await gate.beforeInstruction(abort.signal); steps++; await gate.beforeInstruction(abort.signal); steps++; })();
await Promise.resolve(); assert.equal(steps,0);
gate.step(); await new Promise(resolve=>setImmediate(resolve)); assert.equal(steps,1);
gate.resume(); await work; assert.equal(steps,2);
gate.pause(); const pending = gate.beforeInstruction(abort.signal); abort.abort();
await assert.rejects(pending,{name:"AbortError"});
assert.equal(gate.waiting.size,0);
console.log("Compiler source traces map helper calls and false branches; shared stepping pauses and aborts cleanly.");
