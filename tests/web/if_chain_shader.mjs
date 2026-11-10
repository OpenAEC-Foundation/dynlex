import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { createWgslTranslator } from "../../web/wgsl-translator.js";

const project = path.resolve(import.meta.dirname, "../..");
const declarations = `flex section when condition:
    replacement:
        @intrinsic("if", condition)
flex section otherwise when condition:
    replacement:
        @intrinsic("else if", condition)
flex section otherwise:
    replacement:
        @intrinsic("else")
to get below value and limit:
    @intrinsic("return", @intrinsic("less than", value, limit))
`;
const chain = `when below x and 1.0:
    @intrinsic("store", shade, 1.0)
otherwise when below x and 2.0:
    @intrinsic("store", shade, 0.25)
otherwise when below x and 3.0:
    @intrinsic("store", shade, 0.75)
`;
const cases = [
  { name: "open", body: chain, expected: [255, 64, 191, 128] },
  {
    name: "closed", expected: [255, 64, 191, 32],
    body: `${chain}otherwise:
    @intrinsic("store", shade, 0.125)
`
  },
  {
    name: "nested", expected: [255, 64, 191, 32],
    body: `when below x and 2.0:
    when below x and 1.0:
        @intrinsic("store", shade, 1.0)
    otherwise:
        @intrinsic("store", shade, 0.25)
otherwise when below x and 3.0:
    @intrinsic("store", shade, 0.75)
otherwise:
    @intrinsic("store", shade, 0.125)
`
  }
];

export async function compileIfChainShaders(compiler = path.join(project, "build/dynlex"), browserBuild) {
  const translator = await createWgslTranslator(await readFile(
    path.join(project, "src/web/ide/public/compiler/dynlex_wgsl_translator.wasm")
  ));
  let browser;
  if (browserBuild) {
    const build = path.resolve(browserBuild);
    const factory = (await import(pathToFileURL(path.join(build, "dynlex_web.js")))).default;
    browser = await factory({ locateFile: file => path.join(build, file) });
    browser.ccall("dynlex_web_init", null, [], []);
  }
  const directory = await mkdtemp(path.join(os.tmpdir(), "dynlex-if-chain-"));
  const shaders = [];
  try {
    for (const test of cases) {
      const source = `${declarations}
@intrinsic("store", x, @intrinsic("extract element", @intrinsic("shader input", "FragCoord"), 0))
@intrinsic("store", shade, 0.5)
${test.body}
@intrinsic("shader output", @intrinsic("construct", @intrinsic("vector", 4), shade, shade, shade, 1.0))
`;
      const entry = path.join(directory, "main.dl");
      const output = path.join(directory, "main.spv");
      await writeFile(entry, source);
      for (const optimization of ["-O0", "-O2"]) {
        execFileSync(path.resolve(compiler), [entry, optimization, "--emit-spirv", "--shader-stage=fragment", "-o", output],
          { cwd: project, timeout: 15000, stdio: "pipe" });
        execFileSync("spirv-val", ["--target-env", "vulkan1.3", output], { timeout: 5000 });
        const translated = translator.translateBoth(await readFile(output), "fragment");
        assert.match(translated.webgpu, /@fragment/);
        assert.match(translated.webgl, /void main/);
        shaders.push({ name: `${test.name} ${optimization}`, expected: test.expected, ...translated });
      }
      if (browser) {
        browser.ccall("dynlex_web_set_main_source", null, ["string"], [source]);
        const status = browser.ccall("dynlex_web_compile_and_emit_shader_spirv", "number", ["string"], ["fragment"]);
        assert.equal(status, 0, browser.ccall("dynlex_web_get_diagnostics_json", "string", [], []));
        const bytes = Buffer.from(browser.ccall("dynlex_web_get_output_shader_spirv_base64", "string", [], []), "base64");
        shaders.push({ name: `${test.name} browser`, expected: test.expected, ...translator.translateBoth(bytes, "fragment") });
      }
    }
    return shaders;
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === import.meta.filename) {
  const shaders = await compileIfChainShaders(process.argv[2], process.argv[3]);
  console.log(`Translated ${shaders.length} runtime if-chain shaders to WGSL and GLSL.`);
}
