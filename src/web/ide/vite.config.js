import { defineConfig } from "vite";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const configDirectory = path.dirname(fileURLToPath(import.meta.url));
const wgslTranslatorPath = path.resolve(configDirectory, "../../../web/wgsl-translator.js");

function serveWgslTranslator() {
  return {
    name: "dynlex-wgsl-translator",
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
  plugins: [serveWgslTranslator()],
  server: {
    host: true,
    port: 5173
  }
});
