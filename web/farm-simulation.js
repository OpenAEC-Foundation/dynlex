import { advanceRoutine, stepRoutine, newRoutine } from "./farm-program.js";
import { perform, sense } from "./farm-actions.js";
import { actors, createWorker, advancePlayer, neighbors, notice, walkable } from "./farm-world.js";
import { updateLivestock } from "./farm-livestock.js";
import { penAt } from "./farm-boundaries.js";

export const ORDERS = [
  { title: "A neighbor's new kitchen", items: { carrots: 4, logs: 2 } },
  { title: "Breakfast at the mill", items: { wheat: 4, eggs: 2 } },
  { title: "The village garden", items: { milk: 2, fertilizer: 2 } }
];
export const currentOrder = world => ORDERS[world.orders % ORDERS.length];
export const canDeliver = world => Object.entries(currentOrder(world).items).every(([kind, count]) => world.stock[kind] >= count);
export const orderReward = world => world.orders % ORDERS.length === 1
  ? world.expanded ? "More seeds" : "A cow, the east field and seeds"
  : world.workers.length < 6 ? "A new worker and seeds" : "More seeds";

export function deliverOrder(world) {
  if (!canDeliver(world)) return false;
  const index = world.orders % ORDERS.length;
  let spawn = null;
  if (index !== 1 && world.workers.length < 6) {
    spawn = world.tiles.flat().filter(t => walkable(world, t.x, t.y)).sort((a, b) =>
      Math.abs(a.x - 4) + Math.abs(a.y - 13) - Math.abs(b.x - 4) - Math.abs(b.y - 13))[0];
    if (!spawn) return false;
  }
  const order = currentOrder(world);
  const gift = orderReward(world);
  for (const [kind, count] of Object.entries(order.items)) world.stock[kind] -= count;
  if (index === 1 && !world.expanded) {
    world.expanded = true;
    const spawn = world.tiles.flat().filter(t => penAt(t.x,t.y)?.id === "cow" && walkable(world,t.x,t.y))
      .sort((a,b) => Math.abs(a.x-world.pasture.x)+Math.abs(a.y-world.pasture.y)-Math.abs(b.x-world.pasture.x)-Math.abs(b.y-world.pasture.y))[0];
    world.cow = { id: "cow", x: spawn.x, y: spawn.y, direction: 2, feed: 0, progress: 0, produce: 0 };
  }
  if (spawn) {
    const id = world.workers.length + 1;
    world.workers.push(createWorker(`worker-${id}`, ["Ada", "Bo", "Nell", "Otis", "Pip", "Rue"][id - 1], spawn.x, spawn.y));
  }
  world.stock.seeds += 12;
  world.orders++;
  world.delivered.push(order.title);
  notice(world, `Delivered: ${order.title}. Received: ${gift}.`);
  return true;
}

export function updateEnvironment(world) {
  world.raining = world.tick % 100 >= 75;
  // Channels carry pond water only through connected low ground.
  const queue = world.tiles.flat().filter(tile => tile.type === "pond");
  for (const tile of world.tiles.flat()) tile.water = tile.type === "pond";
  for (let i = 0; i < queue.length; i++) for (const p of neighbors(queue[i])) {
    const tile = world.tiles[p.y][p.x];
    if (tile.type === "channel" && !tile.water && tile.height - .16 < .30) {
      tile.water = true;
      queue.push(tile);
    }
  }
  for (const tile of world.tiles.flat()) {
    if (tile.type === "plowed") {
      const irrigated = neighbors(tile).some(p => world.tiles[p.y][p.x].water);
      tile.moisture = world.raining || irrigated ? 10 : Math.max(0, tile.moisture - .18);
      if (tile.crop && tile.moisture > 0) tile.crop.growth = Math.min(1, tile.crop.growth + .025 * (tile.fertilizer ? 2 : 1));
    }
    if (tile.tree?.regrow > 0) {
      tile.tree.regrow--;
      if (tile.tree.regrow === 0) {
        // A worker can stand on a stump; the sapling waits until that square is free.
        if (actors(world).some(a => a.x === tile.x && a.y === tile.y)) tile.tree.regrow = 1;
        else {tile.tree.hits = 3;tile.tree.serial = world.nextEntity++;}
      }
    }
  }
  updateLivestock(world);
  if (world.compost.input > 0) {
    world.compost.age++;
    if (world.compost.age >= 20) {
      world.compost.ready += world.compost.input;
      world.compost.input = 0;
      world.compost.age = 0;
    }
  }
}

export function tickFarm(world, roles, workersRunning = true) {
  world.tick++;
  updateEnvironment(world);
  const player = world.player;
  if (player.target) advancePlayer(world);
  if (!workersRunning) return;
  for (const worker of world.workers) {
    if (worker.role === null || worker.held || worker.paused) continue;
    const role = roles.find(role => role.id === worker.role);
    if (role.program === null) { worker.status = "Apply this role's code to start working."; continue; }
    worker.status = advanceRoutine(worker.routine, role.program,
      (...args) => sense(world, worker, ...args),
      (...args) => perform(world, worker, ...args));
  }
}

export function assignRole(worker, role) {
  worker.role = role;
  worker.routine = newRoutine();
  worker.subject = null;
  worker.target = null;
  worker.status = role === null ? "Choose a role for me." : "Starting my role.";
}

export function stepWorker(world, roles, worker) {
  worker.paused = true;
  const role = roles.find(role => role.id === worker.role);
  worker.status = stepRoutine(worker.routine, role.program,
    (...args) => sense(world, worker, ...args),
    (...args) => perform(world, worker, ...args)).message;
}
