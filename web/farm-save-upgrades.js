import { buildFarmPlan, FARM_PREFIX } from "./farm-program.js";

// Rebuild migrated programs once, preserving each worker's control-flow state.
export async function upgradeFarmPrograms(saved, compileDynLex) {
  if (saved === null) return;
  for (const role of saved.roles) {
    if (!role.needsRebuild) continue;
    const compiled = await compileDynLex(FARM_PREFIX + role.applied);
    if (compiled.compileResult.status !== 0) throw new Error("A saved farm program could not be upgraded.");
    const mapped = await buildFarmPlan(compiled.wasm);
    const structure = code => code.map(({op,to,end,count})=>({op,to,end,count}));
    if (JSON.stringify(structure(mapped)) !== JSON.stringify(structure(role.program))) throw new Error("Upgrading a saved role changed its control flow.");
    role.program = mapped;
    role.needsRebuild = false;
  }
}
