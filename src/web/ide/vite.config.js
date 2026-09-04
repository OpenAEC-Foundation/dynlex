import { createHash } from "node:crypto";
import { readFileSync, readdirSync } from "node:fs";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";

const configDirectory = path.dirname(fileURLToPath(import.meta.url));
const compilerDirectory = path.resolve(configDirectory, "public/compiler");
const wgslTranslatorPath = path.resolve(configDirectory, "../../../web/wgsl-translator.js");
const compilerAssetPaths = readdirSync(compilerDirectory, { withFileTypes: true })
  .filter((entry) => entry.isFile() && entry.name !== ".gitkeep")
  .map((entry) => ({ name: `compiler/${entry.name}`, path: path.join(compilerDirectory, entry.name) }));
compilerAssetPaths.push({ name: "wgsl-translator.js", path: wgslTranslatorPath });
compilerAssetPaths.sort((left, right) => left.name.localeCompare(right.name));

const compilerRevisionHash = createHash("sha256");
for (const asset of compilerAssetPaths) {
  compilerRevisionHash.update(asset.name);
  compilerRevisionHash.update("\0");
  compilerRevisionHash.update(readFileSync(asset.path));
}
const compilerRevision = compilerRevisionHash.digest("hex");
const compilerManifest = `${JSON.stringify({ revision: compilerRevision })}\n`;

function serveCompilerAssets() {
  return {
    name: "dynlex-compiler-assets",
    async buildStart() {
      this.emitFile({
        type: "asset",
        fileName: "wgsl-translator.js",
        source: await readFile(wgslTranslatorPath, "utf8")
      });
      this.emitFile({
        type: "asset",
        fileName: "compiler/manifest.json",
        source: compilerManifest
      });
    },
    configureServer(server) {
      server.middlewares.use(async (request, response, next) => {
        const pathname = new URL(request.url ?? "/", "http://localhost").pathname;
        if (pathname === "/compiler/manifest.json") {
          response.statusCode = 200;
          response.setHeader("Content-Type", "application/json; charset=utf-8");
          response.end(request.method === "HEAD" ? undefined : compilerManifest);
          return;
        }
        if (pathname !== "/wgsl-translator.js") {
          next();
          return;
        }
        try {
          const source = await readFile(wgslTranslatorPath);
          response.statusCode = 200;
          response.setHeader("Content-Type", "text/javascript; charset=utf-8");
          response.end(request.method === "HEAD" ? undefined : source);
        } catch (error) {
          next(error);
        }
      });
    }
  };
}

export default defineConfig({
  plugins: [serveCompilerAssets()],
  define: {
    __DYNLEX_COMPILER_REVISION__: JSON.stringify(compilerRevision)
  },
  server: {
    host: true,
    port: 5173
  }
});
