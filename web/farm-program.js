import { ACTIONS, CONDITIONS, DESTINATIONS, ITEMS, ROLE_NAMES, DIRECTIONS, FEATURES, instruction as makeInstruction, NO_TARGET, SUBJECT_ITEMS } from "./farm-vocabulary.js";
import { executionOutput } from "./execution-trace.js";

export const FARM_PREFIX = "import lib/farm_challenge.dl\n\n";

// Read the compiler-generated plan, never the user's DynLex source.
export function parseFarmPlan(stdout) {
  const records = executionOutput(stdout).filter(record => record.text !== null);
  const lines = records.map(record => record.text);
  if (lines.length > 4096) throw new Error("A role can contain at most 4096 instructions.");
  let cursor = 0;
  const code = [];
  function block(nested = false, depth = 0) {
    if (depth > 64) throw new Error("Role blocks are nested too deeply.");
    while (cursor < lines.length) {
      const range = records[cursor].range;
      const [op, first, second, ...extra] = lines[cursor++].split("|");
      if (op === "END") {
        if (!nested || first !== undefined || extra.length) throw new Error("Unexpected block ending.");
        return;
      }
      if (op === "DO" || op === "IF") {
        const id = Number(first), argument = Number(second);
        const entry = (op === "DO" ? ACTIONS : CONDITIONS)[id];
        if (!entry || !Number.isInteger(id) || !Number.isInteger(argument)) throw new Error("Unknown farm instruction.");
        const items = entry.operands.includes("items") || entry.operands === "stock";
        const options = items ? ITEMS : entry.operands === "direction" ? DIRECTIONS : entry.operands.startsWith("feature") ? FEATURES : entry.operands === "role" ? ROLE_NAMES : null;
        if ((options && (argument < (items ? SUBJECT_ITEMS : 0) || argument >= options.length)) || (!options && argument !== 0)) throw new Error("Unknown farm argument.");
        const amount=Number(extra[0]), target=Number(extra[1]), area=extra.slice(2).join("|");
        if (extra.length<3 || !Number.isInteger(amount) || amount < -1 || (entry.operands === "stock" && amount<0)) throw new Error("Use a nonnegative whole number of items.");
        if (!Number.isInteger(target) || target<NO_TARGET || target>=DESTINATIONS.length) throw new Error("Unknown farm target.");
        if (area.length>40) throw new Error("Work area names have at most 40 characters.");
        const instruction = makeInstruction(op === "DO" ? "action" : "test", entry.key, {argument,amount,target,area,range});
        code.push(instruction);
        if (op === "IF") {
          block(true, depth + 1);
          if (lines[cursor] === "ELSE") {
            const jump = { op: "jump", to: 0, range: records[cursor++].range };
            code.push(jump);
            instruction.to = code.length;
            block(true, depth + 1);
            jump.to = code.length;
          } else instruction.to = code.length;
        }
      } else if (op === "LOOP") {
        const count = Number(first);
        if (!Number.isInteger(count) || count < -1 || count > 10000 || second !== undefined || extra.length) throw new Error("Use a loop count between 0 and 10000, or loop forever.");
        const start = code.length;
        code.push({ op: "loop", count, range });
        block(true, depth + 1);
        code.push({ op: "next", to: start, range });
        code[start].end = code.length;
      } else throw new Error("Use farm commands inside the role.");
    }
    if (nested) throw new Error("Unclosed farm block.");
  }
  block();
  if (!code.some(instruction => instruction.op === "action")) throw new Error("Give this role an action, such as wait.");
  return code;
}

export function newRoutine() { return { pc: 0, loops: {}, finished: false, trace: null, continuation: null }; }

export function stepRoutine(routine, code, sense, act) {
  if (routine.finished || routine.pc === code.length) {
    routine.finished = true;
    return { message: "Finished this role. Add loop forever to repeat.", done: true };
  }
  const instruction = code[routine.pc];
  routine.trace = { range: instruction.range, branch: null, message: "" };
  if (instruction.op === "action") {
    const result = act(instruction, routine.continuation);
    routine.continuation = result.done ? null : result.continuation;
    if (result.done) routine.pc++;
    routine.trace.message = result.message;
    return { message: result.message, done: !instruction.key.startsWith("focus") };
  }
  if (instruction.op === "test") {
    const branch = sense(instruction);
    routine.trace.branch = branch;
    routine.trace.message = `Condition is ${branch ? "true" : "false"}.`;
    routine.pc = branch ? routine.pc + 1 : instruction.to;
  } else if (instruction.op === "jump") {
    routine.pc = instruction.to; routine.trace.message = "Continue after this branch.";
  } else if (instruction.op === "loop") {
    if (instruction.count === 0) routine.pc = instruction.end;
    else {
      if (!(routine.pc in routine.loops)) routine.loops[routine.pc] = instruction.count;
      routine.pc++;
    }
    routine.trace.message = instruction.count === 0 ? "Skip this loop." : "Start this loop.";
  } else if (instruction.op === "next") {
    const start = instruction.to;
    if (code[start].count === -1 || --routine.loops[start] > 0) routine.pc = start + 1;
    else { delete routine.loops[start]; routine.pc++; }
    routine.trace.message = routine.pc === start + 1 ? "Repeat this loop." : "Loop finished.";
  } else throw new Error("Invalid farm program counter.");
  return { message: routine.trace.message, done: false };
}

export function advanceRoutine(routine, code, sense, act) {
  if (routine.finished) return "Finished this role. Add loop forever to repeat.";
  let condition = null;
  // A loop with no action is valid user code. Bound its work per farm tick.
  for (let budget = 0; budget < 128; budget++) {
    const result = stepRoutine(routine, code, sense, act);
    if (routine.trace !== null && routine.trace.branch !== null) condition = { range: routine.trace.range, branch: routine.trace.branch };
    if (result.done) {
      if (condition !== null) routine.trace.condition = condition;
      return result.message;
    }
  }
  return "Waiting for a condition. Put wait inside an empty loop.";
}

export function buildFarmPlan(wasm, { signal } = {}) {
  return new Promise((resolve, reject) => {
    signal?.throwIfAborted();
    const worker = new Worker(new URL("./farm-program-worker.js", import.meta.url), { type: "module" });
    let timer, finished = false;
    const finish = (error, stdout) => {
      if (finished) return;
      finished = true;
      clearTimeout(timer); worker.onmessage = worker.onerror = null; worker.terminate(); signal?.removeEventListener("abort", abort);
      if (error) reject(error);
      else { try { resolve(parseFarmPlan(stdout)); } catch (error) { reject(error); } }
    };
    const abort = () => finish(signal.reason);
    const deadline = (milliseconds, message) => {
      clearTimeout(timer);
      timer = setTimeout(() => finish(new Error(message)), milliseconds);
    };
    // Fetching modules and compiling/instantiating WASM are startup work. Only
    // begin the execution budget when the worker enters the user's main().
    deadline(30000, "Farm role runtime did not finish loading within 30 seconds.");
    signal?.addEventListener("abort", abort, { once: true });
    worker.onmessage = ({ data }) => {
      if (data.type === "loaded") worker.postMessage({ type: "prepare", wasm });
      else if (data.type === "ready") worker.postMessage({ type: "run" });
      else if (data.type === "started") deadline(5000, "Role execution exceeded the five-second limit.");
      else if (data.type === "result") finish(null, data.stdout);
      else if (data.type === "error") finish(new Error(data.error));
      else finish(new Error("Farm role worker returned an unknown message."));
    };
    worker.onerror = error => { console.error("Farm role worker failed", error); finish(new Error("An error occurred. Check the browser log.")); };
  });
}
