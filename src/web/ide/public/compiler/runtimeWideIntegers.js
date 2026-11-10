// LLVM's WebAssembly ABI passes i128 as two i64 limbs and returns it through
// the first (i32) argument. Wider operations are expanded by LLVM using these
// compiler-runtime operations; every limb stays a BigInt across the host ABI.
const binaryOperations = {
  __multi3: (left, right) => left * right,
  __divti3: (left, right) => BigInt.asIntN(128, left) / BigInt.asIntN(128, right),
  __udivti3: (left, right) => left / right,
  __modti3: (left, right) => BigInt.asIntN(128, left) % BigInt.asIntN(128, right),
  __umodti3: (left, right) => left % right
};

const shiftOperations = {
  __ashlti3: (value, amount) => value << amount,
  __ashrti3: (value, amount) => BigInt.asIntN(128, value) >> amount,
  __lshrti3: (value, amount) => value >> amount
};

const floatingConversions = {
  __floattisf: [false, 24], __floattidf: [false, 53],
  __floatuntisf: [true, 24], __floatuntidf: [true, 53]
};
const integerConversions = {
  __fixsfti: false, __fixdfti: false, __fixunssfti: true, __fixunsdfti: true
};

export const wideIntegerImportNames = [
  ...Object.keys(binaryOperations), ...Object.keys(shiftOperations),
  ...Object.keys(floatingConversions), ...Object.keys(integerConversions),
  "__cmpti2", "__ucmpti2"
];

function fromLimbs(low, high) {
  return BigInt.asUintN(64, low) | (BigInt.asUintN(64, high) << 64n);
}

function toFloat(value, precision) {
  const negative = value < 0n;
  let magnitude = negative ? -value : value;
  const dropped = magnitude.toString(2).length - precision;
  if (dropped > 0) {
    const shift = BigInt(dropped);
    const remainder = magnitude & ((1n << shift) - 1n);
    const halfway = 1n << (shift - 1n);
    let significand = magnitude >> shift;
    if (remainder > halfway || (remainder === halfway && (significand & 1n))) significand++;
    magnitude = significand << shift;
  }
  const result = (negative ? -1 : 1) * Number(magnitude);
  return precision === 24 ? Math.fround(result) : result;
}

export function createWideIntegerImports(memory) {
  const imports = {};
  const store = (pointer, value) => {
    const offset = pointer >>> 0;
    const view = new DataView(memory.buffer);
    view.setBigUint64(offset, BigInt.asUintN(64, value), true);
    view.setBigUint64(offset + 8, BigInt.asUintN(64, value >> 64n), true);
  };
  for (const [name, operation] of Object.entries(binaryOperations)) {
    imports[name] = (pointer, leftLow, leftHigh, rightLow, rightHigh) => {
      store(pointer, operation(fromLimbs(leftLow, leftHigh), fromLimbs(rightLow, rightHigh)));
    };
  }
  for (const [name, operation] of Object.entries(shiftOperations)) {
    imports[name] = (pointer, low, high, amount) => store(pointer, operation(fromLimbs(low, high), BigInt(amount)));
  }
  for (const [name, [unsigned, precision]] of Object.entries(floatingConversions)) {
    imports[name] = (low, high) => {
      const bits = fromLimbs(low, high);
      return toFloat(unsigned ? bits : BigInt.asIntN(128, bits), precision);
    };
  }
  for (const [name, unsigned] of Object.entries(integerConversions)) {
    imports[name] = (pointer, value) => {
      const integer = BigInt(Math.trunc(value));
      const normalized = unsigned ? BigInt.asUintN(128, integer) : BigInt.asIntN(128, integer);
      if (normalized !== integer) throw new WebAssembly.RuntimeError("floating-point value exceeds the integer range");
      store(pointer, integer);
    };
  }
  for (const [name, unsigned] of [["__cmpti2", false], ["__ucmpti2", true]]) {
    imports[name] = (leftLow, leftHigh, rightLow, rightHigh) => {
      const left = unsigned ? fromLimbs(leftLow, leftHigh) : BigInt.asIntN(128, fromLimbs(leftLow, leftHigh));
      const right = unsigned ? fromLimbs(rightLow, rightHigh) : BigInt.asIntN(128, fromLimbs(rightLow, rightHigh));
      return left < right ? 0 : left > right ? 2 : 1;
    };
  }
  return imports;
}
