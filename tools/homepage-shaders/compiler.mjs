import fs from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { LspSession } from "../../web/lsp-client.js";
import { createWgslTranslator } from "../../web/wgsl-translator.js";

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
  const translatorPath = path.join(compilerDirectory, "dynlex_wgsl_translator.wasm");
  const translator = await createWgslTranslator(await fs.readFile(translatorPath));
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
  const lsp = new LspSession((message) => {
    const response = compiler.ccall(
      "dynlex_web_lsp_exchange_json",
      "string",
      ["string"],
      [JSON.stringify(message)]
    );
    return JSON.parse(response);
  });
  lsp.onRequest("workspace/semanticTokens/refresh", () => null);
  const initializeResult = await lsp.start({
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
  const document = await lsp.openDocument({
    uri: "file:///workspace/homepage-shader.dl",
    languageId: "dynlex",
    text: ""
  });
  const semanticTokensBySource = new Map();

  async function semanticTokensFor(source) {
    const cached = semanticTokensBySource.get(source);
    if (cached) {
      return cached;
    }
    await document.replaceText(source);
    const payload = await document.request("textDocument/semanticTokens/full");
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
        "dynlex_web_compile_and_emit_shader_spirv",
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

      const spirvBase64 = compiler.ccall(
        "dynlex_web_get_output_shader_spirv_base64",
        "string",
        [],
        []
      );
      const uniformPayload = parseCompilerJson(
        compiler.ccall("dynlex_web_get_shader_uniforms_json", "string", [], []),
        "shader-uniform"
      );
      const semanticTokens = await semanticTokensFor(source);
      const wgsl = translator.translate(Uint8Array.from(Buffer.from(spirvBase64, "base64")));
      if (!wgsl.includes(`@${stage}`) || !wgsl.includes("fn main")) {
        throw new Error(`${sourceName} produced invalid WebGPU ${stage} source`);
      }
      if (!Array.isArray(uniformPayload.uniforms)) {
        throw new Error(`${sourceName} produced invalid shader-uniform reflection`);
      }

      return Object.freeze({
        wgsl,
        uniforms: uniformPayload.uniforms,
        semanticTokens,
        semanticLegend
      });
    },

    async close() {
      await document.close();
      await lsp.stop();
    }
  });
}
