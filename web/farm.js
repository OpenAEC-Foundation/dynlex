import { createAreaControls } from "./farm-area-controls.js";
import { FARM_GOALS, createCodingGoals } from "./coding-goals.js";
import { createFarm, actors, animals, actorById, advancePlayer, notice, relocate, draggableById, addFlag, removeFlag, moveFlag, tileAt, unlocked, walkable } from "./farm-world.js";
import { perform, plannedRoute } from "./farm-actions.js";
import { createRoles, createRole, copyRole } from "./farm-roles.js";
import { assignRole, canDeliver, currentOrder, deliverOrder, orderReward, tickFarm, stepWorker } from "./farm-simulation.js";
import { showExecution } from "./execution-trace.js";
import { upgradeFarmPrograms } from "./farm-save-upgrades.js";
import { FARM_PREFIX, buildFarmPlan } from "./farm-program.js";
import { FLAGS, ACTIONS, DESTINATIONS, ITEMS, itemInfo, targetId, action, example, MANUAL_ACTIONS } from "./farm-vocabulary.js";
import { createFarmRenderer } from "./farm-renderer.js";

const required = (scope, selector) => {
  const element = scope.querySelector(selector);
  if (!element) throw new Error(`Missing farm element ${selector}`);
  return element;
};
const inventoryText = actor => actor.inventory.length ? actor.inventory.map(item => `${itemInfo(item.kind).label}${item.kind === "bucket" ? ` (${item.water}/4 water)` : item.count > 1 ? ` ×${item.count}` : ""}`).join(" · ") : "Two free hands";

function markup() {
  return `<article class="farm-game" data-farm-game aria-label="Programmable farm">
    <header class="farm-header"><div><span>CHALLENGE 02 · A FARM OF YOUR OWN</span><h3>Meadow Farm</h3></div>
      <div class="farm-weather"><b data-farm-weather>☀ CLEAR SKIES</b><small data-farm-day>DAY 1</small></div></header>
    <div class="farm-layout">
      <section class="farm-landscape" aria-label="3D farm and supplies">
        <div class="farm-scene"><div class="farm-scene-caption"><span>MEADOW FARM</span><small>20 × 20</small></div>
          <div class="farm-board" data-farm-board></div>
          <div class="farm-camera"><button data-farm-view="farm">WHOLE FARM</button><button data-farm-view="selected">VIEW SELECTION</button></div>

        </div>
        <p class="farm-controls-hint">Click ground to walk · drag workers, chickens or flags to place them · right-drag to orbit · scroll to zoom</p>
        <p class="farm-controls-hint" data-farm-route hidden></p>
        <div class="farm-flags"><div data-farm-flags aria-label="Placed flags"></div>
          <label>NEW FLAG <select data-farm-flag-color aria-label="New flag color"></select></label><button data-farm-add-flag>+ ADD FLAG</button></div>
        <div data-farm-areas></div>
        <div class="farm-playbar"><button data-farm-play>▶ START FARM</button><button data-farm-step>ONE TURN</button>
          <label>PACE <select data-farm-speed aria-label="Farm speed"><option value="1">1×</option><option value="2">2×</option></select></label>
          <span data-farm-tick>TURN 0</span><button class="farm-reset" data-farm-reset>RESET</button></div>
        <div class="farm-worker-bar" data-farm-workers aria-label="Select farmer or worker"></div>
        <p class="farm-message" data-farm-message role="status" aria-live="polite">Choose a worker and give them a role.</p>
        <div class="farm-bottom-grid">
          <section class="farm-storage"><div class="farm-card-heading"><h4>THE BARN</h4><button data-farm-go-barn>WALK HERE</button></div>
            <p>Shared supplies. Each person carries two items or bags.</p><div class="farm-stock" data-farm-stock></div></section>
          <section class="farm-order"><span class="farm-eyebrow">A NOTE FROM THE VILLAGE</span><h4 data-farm-order-title></h4>
            <div data-farm-order-items></div><p data-farm-order-reward></p><button data-farm-deliver>DELIVER ORDER</button>
            <small data-farm-order-count>0 orders delivered</small></section>
        </div>
        <section data-farm-goals></section>
      </section>
      <aside class="farm-workshop">
        <div class="farm-person-heading"><div class="farm-person-avatar" data-farm-person-avatar>A</div><div><small data-farm-person-kind>WORKER</small><h4 data-farm-person-name>Ada</h4></div><button data-farm-select-player>CONTROL YOU</button></div>
        <p class="farm-person-status" data-farm-person-status></p><div class="farm-hands" data-farm-hands></div>
        <section data-farm-role-panel>
          <div class="farm-new-role"><input data-farm-worker-name aria-label="Worker name" maxlength="40"><button data-farm-rename-worker>RENAME WORKER</button></div>
          <div class="farm-role-tools"><button data-farm-pause-worker>PAUSE WORKER</button><button data-farm-step-worker>ONE INSTRUCTION</button></div>
          <p class="farm-role-hint">Lumberjack is your example. Create a role and write the instructions for your next job.</p>
          <label class="farm-field-label" for="farm-role">ROLE <span>One program shared by everyone in this role.</span></label>
          <select id="farm-role" data-farm-role aria-label="Worker role"></select>
          <div class="farm-new-role"><input data-farm-role-name aria-label="New role name" placeholder="Name a new role" maxlength="40"><button data-farm-add-role>+ CREATE</button></div>
          <div class="farm-role-tools"><button data-farm-copy-role>COPY ROLE</button><button data-farm-delete-role>DELETE ROLE</button></div>
          <div class="farm-code-label"><span data-farm-code-title>LUMBERJACK.DL</span><small data-farm-code-state>READY TO ASSIGN</small></div>
          <div class="farm-editor" data-farm-editor></div>
          <p class="farm-role-hint" data-farm-execution></p>
          <div class="farm-diagnostics" data-farm-diagnostics role="status" hidden></div>
          <button class="farm-apply" data-farm-apply>GIVE THIS ROLE TO ADA</button>
          <button class="farm-idle" data-farm-idle>REMOVE ROLE</button>
          <details class="farm-help"><summary>Words your workers understand</summary><div data-farm-vocabulary></div>
            <p>Name reusable instructions with <code>to tend a bed:</code>. Use <code>if …:</code>, <code>else:</code>, <code>loop 3 times:</code> or <code>loop forever:</code>. Each action takes a turn; conditions read the farm as it is now.</p>
            <p><code>grab</code>, <code>take</code>, <code>collect</code> and <code>pick up</code> mean the same thing. Use <code>grab fertilizer</code> for items within reach, or <code>grab fertilizer from the compost heap</code> to choose a source. A named item takes up to one bag's worth; a number chooses the amount; <code>everything</code> fills the available hands.</p>
            <p>Flags are movable destinations. <code>walk to the red flag</code> stops beside it and faces its square. Drag that flag to give workers a new work site.</p></details>
        </section>
        <section class="farm-manual" data-farm-manual hidden>
          <h4>Your hands, your decisions.</h4><p>You move and work directly. Only your workers can be programmed.</p>
          <p>Click a bed or tree to walk beside it. Use WASD or the arrow keys while the farm has focus to move one square.</p>
          <label class="farm-field-label">YOUR NEXT ACTION</label><select data-farm-action aria-label="Farmer action"></select><button class="farm-apply" data-farm-act>DO THIS</button>
          <p>Walk beside the barn, then click a supply below to pick it up. Store everything to free your hands.</p>
          <div class="farm-supplies" data-farm-supplies></div>
        </section>
        <details class="farm-help farm-guide" open><summary>How your farm grows</summary>
          <p><b>Fields:</b> hoe → plow, seeds → plant, full bucket → water. Harvest ripe crops and save their scraps. Rain and pond-connected channels keep beds wet.</p>
          <p><b>Animals:</b> start with two chickens. Feed them wheat and collect their eggs. The second village order brings a cow; feed it hay to get milk.</p>
          <p><b>Compost:</b> scraps mature into fertilizer after 20 turns. Fertilized crops grow twice as fast.</p>
          <p><b>Teamwork:</b> give items to adjacent workers, or leave piles for a porter. Fill village orders to welcome more workers and open the east field.</p>
        </details>
      </aside>
    </div></article>`;
}

export async function initializeFarm(section, { compileDynLex, createEditor, saved = null, onSave = () => {} }) {
  await upgradeFarmPrograms(saved, compileDynLex);
  const title = required(section, ".section-title");
  required(title, "p").textContent = "CODING CHALLENGE / 02";
  required(title, "h2").innerHTML = "Grow a farm.<br><span>Teach a team.</span>";
  required(title, "small").textContent = "One worker, two chickens, and your code. Start with an example, then teach your team new jobs.";
  required(section, "[data-challenge-navigation]").hidden = true;
  const mount = required(section, "[data-river-challenge-mount]"); mount.innerHTML = markup();
  const game = required(mount, "[data-farm-game]");
  const get = name => required(game, `[data-farm-${name}]`);
  const board = get("board"), roleSelect = get("role"), diagnostics = get("diagnostics");
  const abort = new AbortController(), events = { signal: abort.signal };
  let world = createFarm(), roles = createRoles(), selected = "worker-1", roleId = "lumberjack";
  let playing = false, speed = 1, interval = null, disposed = false, workerSignature = "", orderSignature = "", flagSignature = "", pendingApply = 0;
  let heldKey = null, manualWalkTimer = null;
  let preparation = null;
  if (saved !== null) {
    ({world,roles,selected,roleId,speed} = saved);
    for (const actor of [...actors(world), ...world.chickens]) actor.held = false;
    for (const role of roles) role.generation = 0;
    get("speed").value = String(speed);
  }
  function save() {
    const snapshot = structuredClone({world,roles,selected,roleId,speed});
    for (const actor of [...actors(snapshot.world), ...snapshot.world.chickens]) actor.held = false;
    for (const role of snapshot.roles) delete role.generation;
    onSave(snapshot);
  }
  window.addEventListener("pagehide", save, events);
  function stopManualWalk() { heldKey = null; clearTimeout(manualWalkTimer); }
  function manualStep() {
    const direction = { ArrowUp: 0, w: 0, ArrowRight: 1, d: 1, ArrowDown: 2, s: 2, ArrowLeft: 3, a: 3 }[heldKey];
    world.player.direction = direction; world.player.target = null;
    world.player.status = perform(world, world.player, action("step")).message;
    sync();
    manualWalkTimer = setTimeout(manualStep, 500 / speed);
  }
  get("add-flag").addEventListener("click", () => {
    const id = get("flag-color").value;
    notice(world, addFlag(world, id) ? `Added the ${id} flag near you. Drag it to its destination.` : "There is no free square for another flag.");
    sync();
  }, events);
  get("flags").addEventListener("click", event => {
    const button = event.target.closest("[data-remove-flag]");
    if (!button) return;
    removeFlag(world, button.dataset.removeFlag);
    notice(world, `Removed the ${button.dataset.removeFlag} flag.`); sync();
  }, events);
  const stockNodes = new Map();
  const updateGoals = createCodingGoals(get("goals"), FARM_GOALS);
  const callbacks = {
    area(start,end) { areaControls.finish(start,end); },
    grab(id, held) { draggableById(world, id).held = held; },
    select(id) {
      if (id === "player" || id.startsWith("worker-")) selectPerson(id);
      else if (id.startsWith("flag-")) { const flag = world.flags.find(f => f.id === id.slice(5)); walkTo(flag, true); }
      else if (id.startsWith("tile-")) { const n = Number(id.slice(5)); walkTo({ x: n % 20, y: Math.floor(n / 20) }, true); }
      else if (id === "cow" || id.startsWith("chicken-")) walkTo(animals(world).find(animal=>animal.id===id), true);
      else if (["barn", "compost"].includes(id)) walkTo(world[id], true);
    },
    drop(id, point) {
      const moved = id.startsWith("flag-") ? moveFlag(world, id.slice(5), point.x, point.y) : relocate(world, id, point.x, point.y);
      notice(world, moved ? id.startsWith("flag-") ? "Flag moved. Workers using it will follow its new position." : id.startsWith("chicken-") ? "Chicken placed. It can explore here." : "Worker placed. Their role continues here." : "Choose a free square on available land.");
      if (moved && id.startsWith("worker-")) selectPerson(id);
      sync();
    },
    walk(point) {
      const tile = tileAt(world, point.x, point.y);
      walkTo(point, Boolean(tile.plot || tile.tree || !walkable(world, point.x, point.y, world.player)));
    },
    key(key, pressed) {
      if (!pressed) { if (key === null || key === heldKey) stopManualWalk(); return; }
      stopManualWalk(); heldKey = key; manualStep();
    }
  };
  let view = createFarmRenderer(board, world, callbacks);
  const areaControls = createAreaControls(get("areas"), () => world, value => view.setAreaDrawing(value), sync, events);
  const editor = await createEditor(get("editor"), {
    value: currentRole()?.source ?? "", prefix: FARM_PREFIX,
    ariaLabel: "Role DynLex code",
    onRun: () => { if (!get("apply").disabled) void applyRole(); }
  });

  if (currentRole()) currentRole().source = editor.getValue();

  function showError(message) { diagnostics.hidden = false; diagnostics.textContent = message; }
  function currentRole() { return roles.find(role => role.id === roleId); }
  function populateRoles() {
    roleSelect.replaceChildren(...roles.map(role => new Option(role.name, role.id)));
    roleSelect.value = roleId;
  }
  function showRole(id) {
    roleId = id; const role = currentRole(); roleSelect.value = id ?? ""; editor.setValue(role?.source ?? "");
    diagnostics.hidden = true; diagnostics.textContent = ""; sync();
  }
  function selectPerson(id) {
    selected = id;
    const actor = actorById(world, id);
    get("worker-name").value = actor.name;
    if (actor.role !== null) showRole(actor.role);
    else sync();
  }
  function walkTo(point, adjacent) {
    if (!unlocked(world, point.x, point.y)) { notice(world, "The east field opens after the second village order."); sync(); return; }
    world.player.target = { x: point.x, y: point.y, adjacent, animal: point.id === "cow" || point.id?.startsWith("chicken-") ? point.id : null };
    world.player.status = "Walking to the chosen square.";
    selected = "player"; sync();
  }
  function doManual(instruction) {
    world.player.target = null;
    world.player.status = perform(world, world.player, instruction).message;
    notice(world, world.player.status); sync();
  }
  function sync() {
    const actor = actorById(world, selected), role = currentRole();
    get("person-name").textContent = actor.name;
    get("person-avatar").textContent = actor.name[0];
    get("person-kind").textContent = actor.id === "player" ? "FARMER · YOU" : "WORKER";
    get("person-status").textContent = actor.paused ? `Paused. ${actor.status}` : actor.status;
    get("hands").textContent = inventoryText(actor);
    get("role-panel").hidden = actor.id === "player";
    get("manual").hidden = actor.id !== "player";
    get("select-player").hidden = actor.id === "player";
    get("code-title").textContent = role ? `${role.name.toUpperCase()}.DL` : "CREATE A ROLE TO START";
    const assigned = role ? world.workers.filter(worker => worker.role === role.id).length : 0;
    const hasCode = Boolean(role && role.source.trim().length > 0);
    get("code-state").textContent = !hasCode ? "WRITE YOUR CODE" : role.source !== role.applied ? role.program ? "UNAPPLIED EDITS" : "READY TO ASSIGN" : `${assigned} WORKER${assigned === 1 ? "" : "S"} USING THIS CODE`;
    editor.editor.updateOptions({ readOnly: !role });
    get("copy-role").disabled = get("delete-role").disabled = !role;
    get("pause-worker").textContent = actor.paused ? "RESUME WORKER" : "PAUSE WORKER";
    const workerRole = roles.find(candidate => candidate.id === actor.role);
    get("step-worker").disabled = !workerRole?.program || actor.held || actor.routine.finished;
    const trace = actor.role === roleId && role && role.source === role.applied ? actor.routine.trace : null;
    showExecution(editor, get("execution"), trace ?? { message: role?.source !== role?.applied ? "Apply your edits to watch this code run." : "Pause this worker and step through their instructions." });
    get("apply").disabled = !hasCode || Boolean(preparation && !preparation.signal.aborted);
    get("apply").textContent = role && actor.role === role.id ? "APPLY TO EVERYONE IN THIS ROLE" : `GIVE THIS ROLE TO ${actor.name.toUpperCase()}`;
    get("idle").disabled = actor.role === null;
    for (const key of ["feedCow", "milk"]) get("action").querySelector(`option[value="${key}"]`).disabled = world.cow === null;
    get("play").textContent = playing ? "Ⅱ PAUSE FARM" : "▶ START FARM";
    get("tick").textContent = `TURN ${world.tick}`;
    get("day").textContent = `DAY ${Math.floor(world.tick / 100) + 1} · ${world.raining ? `${100 - world.tick % 100} turns of rain` : `rain in ${75 - world.tick % 100} turns`}`;
    get("weather").textContent = world.raining ? "☂ RAINING" : "☀ CLEAR SKIES";
    get("message").textContent = world.notices[0];
    game.dataset.playing = String(playing); game.dataset.selected = selected;
    const signature = JSON.stringify([selected, world.workers.map(worker => [worker.id, worker.name, worker.role, worker.paused])]);
    if (signature !== workerSignature) {
      get("workers").replaceChildren(...actors(world).map(person => {
        const button = document.createElement("button");
        button.dataset.farmWorker = person.id;
        button.className = person.id === selected ? "is-selected" : "";
        button.textContent = `${person.name} · ${person.id === "player" ? "farmer" : person.paused ? "paused" : roles.find(r => r.id === person.role)?.name ?? "choose role"}`;
        return button;
      }));
      workerSignature = signature;
    }
    for (const item of ITEMS) stockNodes.get(item.key).textContent = String(world.stock[item.key]);
    const nextFlags = world.flags.map(flag => flag.id).join(",");
    if (nextFlags !== flagSignature) {
      const available = FLAGS.filter(color => !world.flags.some(flag => flag.id === color.id));
      get("flag-color").replaceChildren(...available.map(color => new Option(color.id, color.id)));
      get("add-flag").disabled = available.length === 0;
      get("flags").replaceChildren(...world.flags.map(flag => {
        const button = document.createElement("button");
        button.dataset.removeFlag = flag.id;
        button.style.setProperty("--flag-color", FLAGS.find(color => color.id === flag.id).color);
        button.textContent = `${flag.id} ×`; button.setAttribute("aria-label", `Remove ${flag.id} flag`);
        return button;
      }));
      flagSignature = nextFlags;
    }
    const order = currentOrder(world);
    const nextOrderSignature = JSON.stringify([world.orders, Object.keys(order.items).map(key => world.stock[key])]);
    if (nextOrderSignature !== orderSignature) {
      get("order-title").textContent = order.title;
      get("order-items").replaceChildren(...Object.entries(order.items).map(([kind, count]) => {
        const line = document.createElement("p"); line.textContent = `${itemInfo(kind).label}  ${Math.min(world.stock[kind], count)} / ${count}`;
        line.className = world.stock[kind] >= count ? "is-ready" : ""; return line;
      }));
      get("order-reward").textContent = `THANK YOU GIFT · ${orderReward(world)}`;
      get("order-count").textContent = `${world.orders} order${world.orders === 1 ? "" : "s"} delivered`;
      orderSignature = nextOrderSignature;
    }
    get("deliver").disabled = !canDeliver(world);
    const route = plannedRoute(world, actor, roles.find(candidate => candidate.id === actor.role)?.program);
    get("route").hidden = route === null;
    if (route) get("route").textContent = `${actor.name} → ${route.label} · ${route.path === null ? "waiting for a clear path" : `${route.path.length} steps`}`;
    areaControls.update();
    updateGoals(world.coding);
    view.update(world, selected, .5 / speed, route);
    save();
  }

  async function applyRole() {
    const role = currentRole(), actor = actorById(world, selected), snapshot = world;
    if (actor.id === "player") return;
    preparation?.abort();
    const controller = new AbortController(); preparation = controller;
    const generation = ++role.generation, text = role.source, request = ++pendingApply;
    get("apply").disabled = true; diagnostics.hidden = true; get("code-state").textContent = "COMPILING ROLE…";
    try {
      const compiled = await compileDynLex(FARM_PREFIX + text);
      if (disposed || snapshot !== world || generation !== role.generation) return;
      if (compiled.compileResult.status !== 0) {
        showError(compiled.compileResult.diagnostics.map(d => `Line ${Math.max(1, d.line - 2)}: ${d.message}`).join("\n"));
        return;
      }
      if (controller.signal.aborted) return;
      get("code-state").textContent = "PREPARING ROLE…";
      const program = await buildFarmPlan(compiled.wasm, { signal: controller.signal });
      if (disposed || snapshot !== world || generation !== role.generation) return;
      role.program = program; role.applied = text; role.needsRebuild = false;
      assignRole(actor, role.id);
      for (const worker of world.workers) if (worker.role === role.id) assignRole(worker, role.id);
      playing = true;
      notice(world, `${role.name} applied. Workers with this role now share these instructions.`);
    } catch (error) {
      if (controller.signal.aborted || disposed || snapshot !== world || generation !== role.generation) return;
      console.error("Farm role failed", error);
      showError("An error occurred. Check the browser log.");
    } finally {
      if (preparation === controller) preparation = null;
      if (!disposed && request === pendingApply) sync();
    }
  }

  populateRoles();
  for (const item of ITEMS) {
    const card = document.createElement("div"); card.className = "farm-stock-item";
    const name = document.createElement("span"); name.textContent = item.label;
    const count = document.createElement("b"); count.dataset.farmStock = item.key;
    card.append(name, count); get("stock").append(card); stockNodes.set(item.key, count);
    const button = document.createElement("button"); button.dataset.farmGrab = item.key; button.textContent = `Take ${item.label.toLowerCase()}`;
    get("supplies").append(button);
  }
  get("action").replaceChildren(...MANUAL_ACTIONS.map(entry => new Option(entry.label, entry.id)));
  get("vocabulary").replaceChildren(...[
    "if there's a tree in front of me:", "if i can reach it:", "if my hands are full:", "if i am carrying an axe:",
    "if there are fewer than 6 logs in the barn:", "if there are at least 2 buckets in the barn:",
    'walk to the nearest thirsty crop in the area "Vegetable patch"',
    "if it's raining:", "if the soil in front of me is dry:", "if the crops in front of me are ripe:",
    "if it's still there:", "store 2 logs", "store everything", "water it", "feed it",
    ...ACTIONS.filter(a => !["give", "walk", "focusTarget", "focusItems"].includes(a.key)).map(entry => example(entry)),
    ...DESTINATIONS.map(d => `walk to ${d.phrase}`), ...ITEMS.map(i => `grab ${i.phrase}`), "give logs to a nearby worker"
  ].map(text => { const code = document.createElement("code"); code.textContent = text; return code; }));
  roleSelect.addEventListener("change", () => showRole(roleSelect.value), events);
  editor.onChange(value => { preparation?.abort(); const role = currentRole(); role.source = value; role.generation++; diagnostics.hidden = true; sync(); });
  get("apply").addEventListener("click", () => void applyRole(), events);
  get("idle").addEventListener("click", () => { assignRole(actorById(world, selected), null); sync(); }, events);
  get("add-role").addEventListener("click", () => {
    const name = get("role-name").value.trim();
    if (!name) { get("role-name").focus(); return; }
    const role = createRole(name), id = role.id;
    roles.push(role);
    get("role-name").value = ""; roleId = id; populateRoles(); showRole(id);
    editor.focus();
  }, events);
  get("copy-role").addEventListener("click", () => {
    const role = copyRole(roles, currentRole()); roleId = role.id;
    populateRoles(); showRole(role.id); editor.focus();
  }, events);
  get("delete-role").addEventListener("click", () => {
    preparation?.abort(); pendingApply++;
    const role = currentRole(); role.generation++;
    for (const worker of world.workers) if (worker.role === role.id) assignRole(worker, null);
    roles = roles.filter(candidate => candidate !== role);
    roleId = roles[0]?.id ?? null; populateRoles(); showRole(roleId);
    notice(world, `Deleted ${role.name}. Its workers are ready for a new role.`); sync();
  }, events);
  get("rename-worker").addEventListener("click", () => {
    const name = get("worker-name").value.trim();
    if (!name) { get("worker-name").focus(); return; }
    actorById(world, selected).name = name; sync();
  }, events);
  get("pause-worker").addEventListener("click", () => {
    const actor = actorById(world, selected); actor.paused = !actor.paused; sync();
  }, events);
  get("step-worker").addEventListener("click", () => { stepWorker(world, roles, actorById(world, selected)); sync(); }, events);
  get("workers").addEventListener("click", event => { const button = event.target.closest("[data-farm-worker]"); if (button) selectPerson(button.dataset.farmWorker); }, events);
  get("select-player").addEventListener("click", () => selectPerson("player"), events);
  get("act").addEventListener("click", () => doManual(MANUAL_ACTIONS.find(entry=>entry.id===get("action").value).instruction), events);
  get("supplies").addEventListener("click", event => { const button = event.target.closest("[data-farm-grab]"); if (button) {const item=ITEMS.find(i=>i.key===button.dataset.farmGrab);doManual(action("collect",{argument:item.id,amount:item.capacity,target:targetId("barn")}));} }, events);
  get("go-barn").addEventListener("click", () => walkTo(world.barn, true), events);
  get("play").addEventListener("click", () => { playing = !playing; sync(); }, events);
  get("step").addEventListener("click", () => { playing = false; tickFarm(world, roles); sync(); }, events);
  get("deliver").addEventListener("click", () => { deliverOrder(world); sync(); }, events);
  for (const button of game.querySelectorAll("[data-farm-view]")) button.addEventListener("click", () => view.focus(button.dataset.farmView === "selected" ? selected : "farm"), events);
  function startClock() {
    clearInterval(interval);
    interval = setInterval(() => {
      try {
        if (playing) { tickFarm(world, roles); sync(); }
        else if (world.player.target) {
          advancePlayer(world); sync();
        }
      } catch (error) { playing = false; clearInterval(interval); console.error("Farm simulation failed", error); showError("An error occurred. Check the browser log."); }
    }, 500 / speed);
  }
  get("speed").addEventListener("change", () => { speed = Number(get("speed").value); startClock(); save(); }, events);
  get("reset").addEventListener("click", () => {
    preparation?.abort();
    areaControls.cancel();
    stopManualWalk();
    for (const role of roles) role.generation++;
    playing = false; world = createFarm(); roles = createRoles(); selected = "worker-1"; roleId = "lumberjack";
    view.dispose(); view = createFarmRenderer(board, world, callbacks);
    get("worker-name").value = actorById(world, selected).name;
    populateRoles(); showRole(roleId); startClock();
  }, events);
  get("worker-name").value = actorById(world, selected).name;
  sync(); startClock(); mount.dataset.challengeLoaded = "farm";
  return { destroy() { disposed = true; preparation?.abort(); stopManualWalk(); for (const role of roles) role.generation++; clearInterval(interval); abort.abort(); editor.dispose(); view.dispose(); } };
}
