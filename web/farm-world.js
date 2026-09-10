import { FARM_GOALS, newGoalProgress } from "./coding-goals.js";
import { FLAGS, itemInfo } from "./farm-vocabulary.js";
import { newRoutine } from "./farm-program.js";
import { canCross, penAt } from "./farm-boundaries.js";

export const SIZE = 20;
export const DIRECTIONS = [[0, -1], [1, 0], [0, 1], [-1, 0]];
export const distance = (a, b) => Math.abs(a.x - b.x) + Math.abs(a.y - b.y);
export const reachable = (world, actor, point) => distance(actor, point) <= 1 && canCross(world, actor, point);
export const inside = (x, y) => x >= 0 && x < SIZE && y >= 0 && y < SIZE;
export const tileAt = (world, x, y) => inside(x, y) ? world.tiles[y][x] : null;
export const ahead = actor => ({ x: actor.x + DIRECTIONS[actor.direction][0], y: actor.y + DIRECTIONS[actor.direction][1] });
export const neighbors = position => DIRECTIONS.map(([dx, dy]) => ({ x: position.x + dx, y: position.y + dy })).filter(p => inside(p.x, p.y));
export const actors = world => [world.player, ...world.workers];
export const animals = world => world.cow === null ? world.chickens : [world.cow, ...world.chickens];
export const actorById = (world, id) => actors(world).find(actor => actor.id === id);

export const draggableById = (world, id) => [...world.workers, ...world.chickens].find(entity => entity.id === id);

export function createWorker(id, name, x, y) {
  return { id, name, x, y, direction: 0, inventory: [], role: null, routine: newRoutine(), subject: null, status: "Choose a role for me.", target: null, held: false, paused: false };
}

export function createFarm() {
  let nextEntity = 1;
  const tiles = Array.from({ length: SIZE }, (_, y) => Array.from({ length: SIZE }, (_, x) => ({
    x, y, height: .42 + Math.sin(x * .7) * .025 + Math.cos(y * .5) * .025,
    type: "grass", plot: false, crop: null, moisture: 0, fertilizer: 0, tree: null, items: [], water: false
  })));
  for (let y = 2; y <= 5; y++) for (let x = 2; x <= 5; x++) {
    if ((x === 2 || x === 5) && y === 2) continue;
    Object.assign(tiles[y][x], { type: "pond", height: .06, water: true });
  }
  for (let y = 5; y <= 7; y++) tiles[y][7].type = "channel";
  for (const [x, y] of [[1, 8], [2, 9], [1, 11], [2, 7], [3, 8], [2, 16], [3, 17], [5, 17], [6, 16],
    [8, 3], [9, 2], [11, 2], [11, 3], [9, 4], [10, 5], [17, 3], [18, 4], [17, 5], [18, 15], [17, 16]]) {
    tiles[y][x].tree = { serial: nextEntity++, hits: 3, regrow: 0 };
  }
  for (const start of [8, 15]) for (let y = 9; y <= 12; y++) for (let x = start; x < start + 3; x++) {
    const tile = tiles[y][x];
    tile.plot = true;
    if (start === 8 && y < 11) {
      tile.type = "plowed";
      tile.moisture = 6;
      tile.crop = { serial: nextEntity++, kind: x === 10 ? "wheat" : "carrots", growth: y === 9 ? 1 : .35 };
    }
  }
  for (let y = 6; y < 18; y++) tiles[y][6].type = "path";
  for (let x = 3; x < 15; x++) tiles[14][x].type = "path";
  return {
    tiles, areas: [], coding: newGoalProgress(FARM_GOALS), tick: 0, raining: false, expanded: false, orders: 0, delivered: [],
    player: { ...createWorker("player", "You", 5, 14), status: "Click the ground to walk." },
    workers: [createWorker("worker-1", "Ada", 4, 12)],
    flags: [{ id: "red", serial:nextEntity++, x: 8, y: 11 }, { id: "blue", serial:nextEntity++, x: 10, y: 11 }, { id: "green", serial:nextEntity++, x: 6, y: 5 }],
    barn: { x: 3, y: 13 }, compost: { x: 4, y: 7, input: 0, ready: 0, age: 0 },
    pasture: { x: 13, y: 3 }, cow: null,
    chickens: [0, 1].map(i => ({ id: `chicken-${i + 1}`, x: 12 + i, y: 7, direction: i * 2, held: false, feed: 0, progress: 0 })),
    stock: { axe: 4, hoe: 4, bucket: 4, shovel: 3, seeds: 48, logs: 0, carrots: 0, wheat: 18, hay: 18, eggs: 0, milk: 0, scraps: 6, fertilizer: 0 },
    notices: ["Start with Ada and two chickens. Try the Lumberjack example, then write your own roles."], nextEntity
  };
}

export const unlocked = (world, x, y) => inside(x, y) && (x < 15 || world.expanded);
export function walkable(world, x, y, actor = null) {
  if (!unlocked(world, x, y)) return false;
  const tile = world.tiles[y][x];
  if (tile.type === "pond" || (tile.tree && tile.tree.regrow === 0)) return false;
  if ([world.barn, world.compost, ...animals(world)].some(p => p !== actor && p.x === x && p.y === y)) return false;
  return !actors(world).some(other => other !== actor && other.x === x && other.y === y);
}

export function face(actor, target) {
  const dx = target.x - actor.x, dy = target.y - actor.y;
  if (dx === 0 && dy === 0) return;
  actor.direction = Math.abs(dx) > Math.abs(dy) ? dx > 0 ? 1 : 3 : dy > 0 ? 2 : 0;
}

export function routeTo(world, actor, target, adjacent = true) {
  const arrived = p => distance(p, target) <= (adjacent ? 1 : 0) && canCross(world, p, target);
  if (arrived(actor)) return [];
  const queue = [{ x: actor.x, y: actor.y, parent: null }];
  const seen = new Set([`${actor.x},${actor.y}`]);
  for (let index = 0; index < queue.length; index++) {
    const current = queue[index];
    for (const next of neighbors(current)) {
      const key = `${next.x},${next.y}`;
      if (seen.has(key) || !walkable(world, next.x, next.y, actor) || !canCross(world, current, next)) continue;
      seen.add(key);
      const node = { ...next, parent: current };
      if (arrived(next)) {
        const route = [];
        for (let step = node; step.parent; step = step.parent) route.push({ x: step.x, y: step.y });
        return route.reverse();
      }
      queue.push(node);
    }
  }
  return null;
}

export function moveToward(world, actor, target, adjacent = true) {
  const route = routeTo(world, actor, target, adjacent);
  if (route === null) return { done: false, message: "Waiting for a clear path." };
  if (route.length) { face(actor, route[0]); Object.assign(actor, route[0]); }
  const done = route.length <= 1;
  if (done) face(actor, target);
  return { done, message: done ? "Arrived." : "Walking." };
}

export function advancePlayer(world) {
  const player = world.player, target = player.target;
  if (target.animal) {
    const animal = animals(world).find(animal=>animal.id === target.animal);
    target.x = animal.x; target.y = animal.y;
  }
  const moved = moveToward(world, player, target, target.adjacent);
  player.status = moved.message;
  if (moved.done) player.target = null;
}

export function relocate(world, id, x, y) {
  const actor = draggableById(world, id);
  if (!actor) return false;
  if (!walkable(world, x, y, actor)) return false;
  Object.assign(actor, { x, y });
  if (world.workers.includes(actor)) Object.assign(actor, { target: null, status: "Placed here. Continuing my role." });
  return true;
}

export function canPlaceFlag(world, id, x, y) {
  if (!unlocked(world, x, y) || world.tiles[y][x].type === "pond") return false;
  if (world.flags.some(flag => flag.id !== id && flag.x === x && flag.y === y)) return false;
  if (world.tiles[y][x].tree || penAt(x, y) || [world.barn, world.compost].some(p => p.x === x && p.y === y)) return false;
  return true;
}
export function moveFlag(world, id, x, y) {
  const flag = world.flags.find(flag => flag.id === id);
  if (!flag) return false;
  if (!canPlaceFlag(world, id, x, y)) return false;
  Object.assign(flag, { x, y });
  return true;
}

export function addFlag(world, id) {
  if (!FLAGS.some(flag => flag.id === id)) throw new Error(`Unknown flag color: ${id}`);
  if (world.flags.some(flag => flag.id === id)) return false;
  const position = world.tiles.flat().filter(tile => canPlaceFlag(world, id, tile.x, tile.y))
    .sort((a, b) => distance(world.player, a) - distance(world.player, b))[0];
  if (!position) return false;
  world.flags.push({ id, serial:world.nextEntity++, x: position.x, y: position.y });
  return true;
}
export function removeFlag(world, id) {
  world.flags = world.flags.filter(flag => flag.id !== id);
}

export const carried = (actor, kind) => actor.inventory.find(item => item.kind === kind);
export function capacity(actor, kind) {
  const info = itemInfo(kind);
  return actor.inventory.filter(item => item.kind === kind).reduce((sum, item) => sum + info.capacity - item.count, 0)
    + (2 - actor.inventory.length) * info.capacity;
}
export function addItem(actor, kind, count = 1, properties = {}) {
  const accepted = Math.min(count, capacity(actor, kind));
  let remaining = accepted;
  for (const item of actor.inventory.filter(item => item.kind === kind)) {
    const amount = Math.min(remaining, itemInfo(kind).capacity - item.count);
    item.count += amount; remaining -= amount;
  }
  while (remaining > 0) {
    const amount = Math.min(remaining, itemInfo(kind).capacity);
    actor.inventory.push({ ...(kind === "bucket" ? { water: 0 } : {}), ...properties, kind, count: amount });
    remaining -= amount;
  }
  return accepted;
}
export function removeItem(actor, kind, count = 1) {
  const items = actor.inventory.filter(item => item.kind === kind);
  if (items.reduce((sum, item) => sum + item.count, 0) < count) return false;
  for (const item of items) { const amount = Math.min(count, item.count); item.count -= amount; count -= amount; }
  actor.inventory = actor.inventory.filter(item => item.count > 0);
  return true;
}
export function dropItem(world, position, kind, count, properties = {}) {
  if (count === 0) return;
  const pile = world.tiles[position.y][position.x].items;
  const existing = pile.find(item => item.kind === kind && item.water === properties.water);
  if (existing) existing.count += count;
  else pile.push({ ...properties, serial:world.nextEntity++, kind, count });
}
export function notice(world, text) { world.notices = [text, ...world.notices].slice(0, 4); }
