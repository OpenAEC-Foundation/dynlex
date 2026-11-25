// Execution state stays in the worker which owns the suspended Wasm stack.
// Addresses are inspected only while paused; references expire on every resume.
export class ProgramDebugger {
  constructor(memory, readString, emit, flush, breakpoints = []) {
    this.memory = memory; this.emit = emit; this.flush = flush;
    this.readString = readString;
    this.frames = []; this.metadata = new Map(); this.references = new Map();
    this.nextReference = 1; this.pause = 0; this.resume = undefined;
    this.stepping = { kind: 'into', depth: 0 }; this.lastYield = performance.now();
    this.setBreakpoints(breakpoints);
  }
  setBreakpoints(points) {
    this.breakpoints = new Map();
    for (const { uri, line } of points) {
      if (typeof uri !== 'string' || !Number.isSafeInteger(line) || line < 0)
        throw new Error('Invalid debugger breakpoint');
      if (!this.breakpoints.has(uri)) this.breakpoints.set(uri, new Set());
      this.breakpoints.get(uri).add(line);
    }
  }
  readMetadata(pointer) {
    if (!this.metadata.has(pointer)) this.metadata.set(pointer, JSON.parse(this.readString(this.memory, pointer)));
    return this.metadata.get(pointer);
  }
  enter(name, location) {
    this.frames.push({ name: this.readString(this.memory, name),
      point: { metadata: { ...this.readMetadata(location), variables: [] }, addresses: 0 } });
  }
  leave() {
    if (!this.frames.length) throw new Error('Debugger call stack underflow');
    this.frames.pop();
  }
  point(metadata, addresses) {
    if (!this.frames.length) throw new Error('Debug location arrived outside a frame');
    this.frames.at(-1).point = { metadata: this.readMetadata(metadata), addresses };
    // Yield to control messages even in an infinite, non-printing loop.
    if (performance.now() - this.lastYield >= 8) {
      return new Promise(resolve => setTimeout(resolve, 0)).then(() => {
        this.lastYield = performance.now(); return this.stopAtPoint();
      });
    }
    return this.stopAtPoint();
  }
  stopAtPoint() {
    const frame = this.frames.at(-1), { uri, range } = frame.point.metadata;
    const breakpoint = this.breakpoints.get(uri)?.has(range.start.line);
    const step = this.stepping;
    const reached = step && (step.kind === 'into' || (step.kind === 'over'
      ? this.frames.length <= step.depth : this.frames.length < step.depth));
    if (!breakpoint && !reached) return;
    this.flush(); this.pause++; this.references.clear();
    return new Promise(resolve => {
      this.resume = resolve;
      this.emit({ type: 'program-paused', pause: this.pause,
        reason: breakpoint ? 'breakpoint' : 'step', frames: this.snapshot() });
    });
  }
  view(address, size) {
    if (!Number.isSafeInteger(address) || address < 0 || address + size > this.memory.buffer.byteLength)
      throw new Error('Debugger inspection is outside program memory');
    return new DataView(this.memory.buffer, address, size);
  }
  integer(address, size) {
    const view = this.view(address, size);
    let value = 0n;
    for (let index = size - 1; index >= 0; index--) value = value * 256n + BigInt(view.getUint8(index));
    return value;
  }
  describe(name, typeIndex, address, types, initialized = true) {
    const type = types[typeIndex];
    const variable = { name, type: type.name, value: '<uninitialized>', reference: 0, children: 0 };
    if (!initialized) return variable;
    switch (type.kind) {
      case 'integer': {
        const raw = this.integer(address, type.size);
        variable.value = (type.signed ? BigInt.asIntN(type.bits, raw) : BigInt.asUintN(type.bits, raw)).toString();
        break;
      }
      case 'boolean': variable.value = this.integer(address, type.size) === 0n ? 'false' : 'true'; break;
      case 'float': {
        const view = this.view(address, type.size);
        if (type.size !== 4 && type.size !== 8) throw new Error('Invalid debugger floating-point layout');
        variable.value = String(type.size === 4 ? view.getFloat32(0, true) : view.getFloat64(0, true));
        break;
      }
      case 'function': variable.value = '0x' + this.integer(address, type.size).toString(16); break;
      case 'pointer': {
        const target = Number(this.integer(address, type.size));
        variable.value = target === 0 ? 'null' : '0x' + target.toString(16);
        variable.children = target !== 0 && Object.hasOwn(type, 'element') ? 1 : 0;
        break;
      }
      case 'array': variable.value = `${type.name} [${type.count}]`; variable.children = type.count; break;
      case 'struct': variable.value = `${type.name} {${type.fields.map(field => field.name).join(', ')}}`;
        variable.children = type.fields.length; break;
      default: throw new Error(`Invalid debugger type kind: ${type.kind}`);
    }
    if (variable.children) {
      variable.reference = this.nextReference++;
      this.references.set(variable.reference, { type, address, types });
    }
    return variable;
  }
  snapshot() {
    return this.frames.toReversed().map(frame => {
      if (!frame.point) throw new Error('A paused call frame has no source location');
      const { metadata, addresses } = frame.point;
      return { name: frame.name, uri: metadata.uri, range: metadata.range,
        variables: metadata.variables.map((variable, index) => ({
          ...this.describe(variable.name, variable.type, this.view(addresses + index * 4, 4).getUint32(0, true),
            metadata.types, variable.initialized), uri: variable.uri, range: variable.range
        })) };
    });
  }
  variables(reference, start = 0, count = 100) {
    if (!this.resume || !this.references.has(reference)) throw new Error('Debugger reference is no longer paused');
    if (!Number.isSafeInteger(start) || !Number.isSafeInteger(count) || start < 0 || count < 1 || count > 1000)
      throw new Error('Invalid debugger variable page');
    const { type, address, types } = this.references.get(reference);
    if (type.kind === 'pointer') {
      const target = Number(this.integer(address, type.size));
      return start === 0 ? [this.describe('*', type.element, target, types)] : [];
    }
    const length = type.kind === 'struct' ? type.fields.length : type.count;
    const result = [];
    for (let index = start; index < Math.min(start + count, length); index++) {
      const field = type.kind === 'struct' ? type.fields[index] : {
        name: `[${index}]`, type: type.element, offset: index * type.stride
      };
      result.push(this.describe(field.name, field.type, address + field.offset, types));
    }
    return result;
  }
  command(payload) {
    if (payload.command === 'breakpoints') { this.setBreakpoints(payload.breakpoints); return null; }
    if (payload.command === 'pause') {
      this.stepping = { kind: 'into', depth: this.frames.length }; return null;
    }
    if (!this.resume || payload.pause !== this.pause) throw new Error('Debugger command belongs to an obsolete pause');
    if (payload.command === 'variables') return this.variables(payload.reference, payload.start, payload.count);
    if (!['into', 'over', 'out', 'continue'].includes(payload.command)) throw new Error('Unknown debugger command');
    this.stepping = payload.command === 'continue' ? undefined : { kind: payload.command, depth: this.frames.length };
    const resume = this.resume; this.resume = undefined; this.references.clear();
    this.emit({ type: 'program-resumed', pause: this.pause });
    resume(); return null;
  }
}
