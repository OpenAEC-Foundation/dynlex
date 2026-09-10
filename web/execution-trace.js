// Both games consume source locations emitted by the compiler at runtime.
export function executionOutput(stdout, prefixLines = 2) {
  let range = null;
  const records = [];
  for (const text of stdout.replaceAll("\r\n", "\n").split("\n").filter(Boolean)) {
    if (!text.startsWith("DYNLEX|TRACE|")) { records.push({ text, range, branch: null }); continue; }
    const parts = text.split("|").slice(2).map(Number);
    if (parts.length !== 5 || !parts.every(Number.isInteger)) throw new Error("Invalid execution source event.");
    const [startLine, startColumn, endLine, endColumn, outcome] = parts;
    if (startLine < prefixLines || endLine < startLine || startColumn < 0 || endColumn < 0 || ![-1,0,1].includes(outcome)) throw new Error("Invalid execution source range.");
    range = { start: { line: startLine - prefixLines, character: startColumn }, end: { line: endLine - prefixLines, character: endColumn } };
    if (outcome !== -1) records.push({ text: null, range, branch: Boolean(outcome) });
  }
  return records;
}

export function showExecution(editor, output, { range = null, branch = null, message = "", condition = null } = {}) {
  editor.decorate(range && {
    startLineNumber: range.start.line + 1, startColumn: range.start.character + 1,
    endLineNumber: range.end.line + 1, endColumn: range.end.character + 1
  });
  output.textContent = branch === null ? message : `Condition is ${branch ? "true · take this branch" : "false · skip this branch"}.`;
  if (condition !== null) output.textContent = `Condition is ${condition.branch ? "true" : "false"}. ${message}`;
}

// Pause at instruction boundaries without polling. Animations may finish within
// one step; the following instruction waits for another step or resume.
export class InstructionGate {
  paused = false;
  credits = 0;
  waiting = new Set();
  pause() { this.paused = true; this.credits = 0; }
  resume() { this.paused = false; this.credits = 0; this.release(); }
  step() { this.paused = true; this.credits++; this.release(); }
  release() { for (const wake of this.waiting) wake(); }
  async beforeInstruction(signal) {
    while (this.paused && this.credits === 0) {
      signal.throwIfAborted();
      await new Promise((resolve, reject) => {
        const wake = () => { cleanup(); resolve(); };
        const abort = () => { cleanup(); reject(signal.reason); };
        const cleanup = () => { this.waiting.delete(wake); signal.removeEventListener("abort", abort); };
        this.waiting.add(wake); signal.addEventListener("abort", abort, { once: true });
      });
    }
    signal.throwIfAborted();
    if (this.paused) this.credits--;
  }
}
