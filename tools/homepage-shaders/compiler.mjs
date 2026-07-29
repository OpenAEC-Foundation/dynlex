import path from "node:path";
import { pathToFileURL } from "node:url";
import {
  initializeLsp,
  LspClient,
  LspTextDocument,
  shutdownLsp
} from "../../web/lsp-client.js";

function parseCompilerJson(text, label) {
  if (typeof text !== "string" || text.length === 0) {
    throw new Error(`Compiler returned no ${label} JSON`);
  }
  try {
    return JSON.parse(text);
  } catch (error) {
    throw new Error(`Compiler returned invalid ${label} JSON`, { cause: error });
  }
}

function validateSemanticTokens(payload, legend) {
  if (
    !payload
    || !Array.isArray(payload.data)
    || payload.data.length === 0
    || payload.data.length % 5 !== 0
    || !legend
    || !Array.isArray(legend.tokenTypes)
    || !Array.isArray(legend.tokenModifiers)
  ) {
    throw new Error("Compiler returned invalid semantic-token data");
  }
}

export async function createHomepageShaderCompiler(projectDirectory) {
  const compilerDirectory = path.join(projectDirectory, "src/web/ide/public/compiler");
  const modulePath = path.join(compilerDirectory, "dynlex_web.js");
  const imported = await import(pathToFileURL(modulePath).href);
  const createModule = imported.default ?? imported;
  if (typeof createModule !== "function") {
    throw new Error(`Expected a module factory export from ${modulePath}`);
  }

  const compiler = await createModule({
    locateFile(fileName) {
      return path.join(compilerDirectory, fileName);
    }
  });
  compiler.ccall("dynlex_web_init", null, [], []);
  const lsp = new LspClient((message) => {
    const response = compiler.ccall(
      "dynlex_web_lsp_exchange_json",
      "string",
      ["string"],
      [JSON.stringify(message)]
    );
    return JSON.parse(response);
  });
  lsp.onRequest("workspace/semanticTokens/refresh", () => null);
  const initializeResult = await initializeLsp(lsp, {
    initializationOptions: {
      dynlex: {
        analysisProfiles: [
          { target: "spirv", shaderStage: "fragment" },
          { target: "spirv", shaderStage: "vertex" }
        ]
      }
    }
  });
  const semanticLegend = initializeResult.capabilities?.semanticTokensProvider?.legend;
  const document = new LspTextDocument(lsp, {
    uri: "file:///workspace/homepage-shader.dl",
    languageId: "dynlex"
  });
  const semanticTokensBySource = new Map();

  async function semanticTokensFor(source) {
    const cached = semanticTokensBySource.get(source);
    if (cached) {
      return cached;
    }
    await document.replaceText(source);
    const payload = await lsp.request("textDocument/semanticTokens/full", {
      textDocument: document.identifier
    });
    validateSemanticTokens(payload, semanticLegend);
    const tokens = Object.freeze([...payload.data]);
    semanticTokensBySource.set(source, tokens);
    return tokens;
  }

  return Object.freeze({
    async compile(source, sourceName, stage) {
      if (stage !== "fragment" && stage !== "vertex") {
        throw new Error(`Unsupported shader stage: ${stage}`);
      }
      compiler.ccall("dynlex_web_set_main_source", null, ["string"], [source]);
      const status = compiler.ccall(
        "dynlex_web_compile_and_emit_shader_glsl",
        "number",
        ["string"],
        [stage]
      );
      const diagnostics = parseCompilerJson(
        compiler.ccall("dynlex_web_get_diagnostics_json", "string", [], []),
        "diagnostics"
      );
      if (status !== 0) {
        throw new Error(`${sourceName} does not compile: ${JSON.stringify(diagnostics)}`);
      }

      const glsl = compiler.ccall("dynlex_web_get_output_shader_glsl", "string", [], []);
      const uniformPayload = parseCompilerJson(
        compiler.ccall("dynlex_web_get_shader_uniforms_json", "string", [], []),
        "shader-uniform"
      );
      const semanticTokens = await semanticTokensFor(source);
      if (!glsl.startsWith("#version 300 es") || !glsl.includes("void main")) {
        throw new Error(`${sourceName} produced invalid WebGL2 ${stage} source`);
      }
      if (!Array.isArray(uniformPayload.uniforms)) {
        throw new Error(`${sourceName} produced invalid shader-uniform reflection`);
      }

      return Object.freeze({
        glsl,
        uniforms: uniformPayload.uniforms,
        semanticTokens,
        semanticLegend
      });
    },

    async close() {
      await document.close();
      await shutdownLsp(lsp);
    }
  });
}
