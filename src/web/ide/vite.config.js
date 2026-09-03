import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";

const compilerWasmPath = fileURLToPath(new URL("./public/compiler/dynlex_web.wasm", import.meta.url));
const compilerRevision = createHash("sha256").update(readFileSync(compilerWasmPath)).digest("hex");

export default defineConfig({
  define: {
    __DYNLEX_COMPILER_REVISION__: JSON.stringify(compilerRevision)
  },
  server: {
    host: true,
    port: 5173
  }
});
