import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { createFarm, createWorker } from "../../web/farm-world.js";
import { createTestTeam } from "./farm_team_fixture.mjs";
import { parseFarmPlan } from "../../web/farm-program.js";
import { assignRole, tickFarm, canDeliver, deliverOrder } from "../../web/farm-simulation.js";

const temporary = mkdtempSync(path.join(tmpdir(), "dynlex-farm-programs-"));
const compiler = path.resolve("build/dynlex");
try {
  execFileSync(compiler, ["tests/required/farm_challenge/main.dl", "-o", path.join(temporary, "fixture.out")]);
  assert.equal(execFileSync(path.join(temporary, "fixture.out"), { encoding: "utf8" }), readFileSync("tests/required/farm_challenge/expected.txt", "utf8"));
  const roles = createTestTeam();
  for (const role of roles) {
    const source = path.join(temporary, `${role.id}.dl`), binary = path.join(temporary, `${role.id}.out`);
    writeFileSync(source, `import lib/farm_challenge.dl\n\n${role.source}\n`);
    execFileSync(compiler, [source, "-o", binary]);
    role.program = parseFarmPlan(execFileSync(binary, { encoding: "utf8" }));
    assert.ok(role.program.length > 0, `${role.name} compiles through DynLex`);
  }
  const world = createFarm(); world.workers = [];
  for (let i = 0; i < roles.length; i++) {
    const worker = createWorker(`worker-${i}`, roles[i].name, 6 + i, 16);
    assignRole(worker, roles[i].id); world.workers.push(worker);
  }
  for (let tick = 0; tick < 900; tick++) {
    tickFarm(world, roles);
    if (world.orders < 2 && canDeliver(world)) deliverOrder(world);
  }
  assert.ok(world.cow, "The test programs earn the cow through village orders");
  for (const kind of ["logs", "carrots", "eggs", "milk", "fertilizer"]) assert.ok(world.stock[kind] > 0, `The team produces and stores ${kind}`);
  assert.equal(world.tiles[7][7].water, true, "The irrigator connects the pond to the channel");
  assert.ok(world.workers.every(worker => worker.inventory.length <= 2));
  assert.equal(new Set(world.workers.map(worker => `${worker.x},${worker.y}`)).size, world.workers.length);
  console.log("The example and test programs compile, earn the cow, and cooperate through 900 farm turns.");
} finally { rmSync(temporary, { recursive: true, force: true }); }
