import { unlocked } from "./farm-world.js";
import { FLAGS } from "./farm-vocabulary.js";

export const inArea = (area, point) => point.x >= area.left && point.x <= area.right && point.y >= area.top && point.y <= area.bottom;
export class AreaInputError extends Error {}

export function areaBounds(world, start, end) {
  const bounds = { left: Math.min(start.x,end.x), top: Math.min(start.y,end.y), right: Math.max(start.x,end.x), bottom: Math.max(start.y,end.y) };
  if (![start,end].every(p => Number.isInteger(p.x) && Number.isInteger(p.y) && unlocked(world,p.x,p.y))) {
    throw new AreaInputError("Choose a rectangle on available land.");
  }
  return bounds;
}

export function addArea(world, name, start, end) {
  name = name.trim();
  if (!name || name.length > 40 || /[\r\n]/.test(name)) throw new AreaInputError("Give your area a name of 1–40 characters.");
  if (world.areas.some(area => area.name === name)) throw new AreaInputError("An area already has that name.");
  const color = FLAGS.find(flag => !world.areas.some(area => area.color === flag.color))?.color ?? FLAGS[world.areas.length % FLAGS.length].color;
  const area = { id: crypto.randomUUID(), name, color, ...areaBounds(world,start,end) };
  world.areas.push(area);
  return area;
}

export function reshapeArea(world, id, start, end) {
  Object.assign(world.areas.find(area => area.id === id), areaBounds(world,start,end));
}

export function removeArea(world, id) { world.areas = world.areas.filter(area => area.id !== id); }
