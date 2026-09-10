import { buildRuntimeImports, createRuntimeFilesystem, inspectRuntimeWasmLayout, isSupportedRuntimeImport } from "./compiler/runtimeImports.js";

let instance;
let stdout = "";
const output = { push(text) {
  stdout += text;
  if (stdout.length > 100000) throw new Error("This role generated too many instructions. Use loop forever for repeating work.");
} };

self.onmessage = async ({ data }) => {
  try {
    if (data.type === "prepare") {
      const module = await WebAssembly.compile(data.wasm);
      const imports = WebAssembly.Module.imports(module);
      if (!imports.every(isSupportedRuntimeImport)) throw new Error("A role used an unsupported browser operation.");
      instance = await WebAssembly.instantiate(module, buildRuntimeImports(imports, output, createRuntimeFilesystem(), inspectRuntimeWasmLayout(data.wasm)));
      self.postMessage({ type: "ready" });
    } else if (data.type === "run") {
      self.postMessage({ type: "started" });
      instance.exports.main();
      self.postMessage({ type: "result", stdout });
    } else throw new Error("Farm role worker received an unknown message.");
  } catch (error) {
    console.error("Farm plan execution failed", error);
    self.postMessage({ type: "error", error: "An error occurred. Check the browser log." });
  }
};

// The runtime dependency uses top-level await. Until it finishes, incoming
// messages can arrive before this handler exists. Ask for the role only now.
self.postMessage({ type: "loaded" });
