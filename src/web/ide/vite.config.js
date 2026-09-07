import { readdirSync } from "node:fs";
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

function serveCompilerAssets() {
  return {
    name: "dynlex-compiler-assets",
    async buildStart() {
      this.emitFile({
        type: "asset",
        fileName: "wgsl-translator.js",
        source: await readFile(wgslTranslatorPath, "utf8")
      });
    },
    configureServer(server) {
      server.middlewares.use(async (request, response, next) => {
        const pathname = new URL(request.url ?? "/", "http://localhost").pathname;
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
  base: process.env.DYNLEX_WEB_BASE ?? "/",
  plugins: [serveCompilerAssets()],
  server: {
    host: true,
    port: 5173
  }
});
