import path from "node:path";
import { pathToFileURL } from "node:url";

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

function validateSemanticTokens(payload) {
  if (
    !payload
    || !Array.isArray(payload.data)
    || payload.data.length === 0
    || payload.data.length % 5 !== 0
    || !payload.legend
    || !Array.isArray(payload.legend.tokenTypes)
    || !Array.isArray(payload.legend.tokenModifiers)
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

  return Object.freeze({
    compile(source, sourceName, stage) {
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
      const semanticPayload = parseCompilerJson(
        compiler.ccall("dynlex_web_get_lsp_semantic_tokens_json", "string", [], []),
        "semantic-token"
      );
      if (!glsl.startsWith("#version 300 es") || !glsl.includes("void main")) {
        throw new Error(`${sourceName} produced invalid WebGL2 ${stage} source`);
      }
      if (!Array.isArray(uniformPayload.uniforms)) {
        throw new Error(`${sourceName} produced invalid shader-uniform reflection`);
      }
      validateSemanticTokens(semanticPayload);

      return Object.freeze({
        glsl,
        uniforms: uniformPayload.uniforms,
        semanticTokens: semanticPayload.data,
        semanticLegend: semanticPayload.legend
      });
    }
  });
}
