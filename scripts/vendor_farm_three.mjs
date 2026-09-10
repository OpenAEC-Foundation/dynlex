#!/usr/bin/env node
import { execFile } from "node:child_process";
import { cp, mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";
import { rolldown } from "../src/web/ide/node_modules/rolldown/dist/index.mjs";

const execute = promisify(execFile);
const project = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const destination = path.join(project, "web/vendor/three");
const temporary = await mkdtemp(path.join(tmpdir(), "dynlex-three-"));
try {
  await execute("npm", ["pack", "three@0.185.1", "--silent"], { cwd: temporary });
  await execute("tar", ["-xzf", "three-0.185.1.tgz", "package/LICENSE", "package/build/three.core.min.js", "package/build/three.module.min.js", "package/examples/jsm/controls/OrbitControls.js"], { cwd: temporary });
  for (const file of ["three.core.min.js", "three.module.min.js"]) {
    await cp(path.join(temporary, "package/build", file), path.join(destination, file));
  }
  await cp(path.join(temporary, "package/LICENSE"), path.join(destination, "LICENSE"));
  const bundle = await rolldown({ input: path.join(temporary, "package/examples/jsm/controls/OrbitControls.js"), external: ["three"] });
  await bundle.write({ file: path.join(destination, "OrbitControls.min.js"), format: "es", minify: true, paths: { three: "./three.module.min.js" }, banner: "// Three.js 0.185.1 OrbitControls. MIT; see LICENSE." });
  await bundle.close();
} finally {
  await rm(temporary, { recursive: true });
}
