export const PENS = [
  { id: "cow", left: 12, right: 14, top: 2, bottom: 4, gate: 13 },
  { id: "chickens", left: 11, right: 13, top: 6, bottom: 8, gate: 12 }
];
export const penAt = (x, y) => PENS.find(p => x >= p.left && x <= p.right && y >= p.top && y <= p.bottom);
const edgeKey = (a, b) => [a, b].map(p => `${p.x},${p.y}`).sort().join(":");
const edges = new Map();
function edge(a, b, gate = false, locked = false) {
  const key = edgeKey(a, b);
  if (edges.has(key)) return;
  const x = (a.x + b.x) / 2, y = (a.y + b.y) / 2;
  const dx = a.x === b.x ? .5 : 0, dy = a.y === b.y ? .5 : 0;
  edges.set(key, { a, b, start: [x - dx, y - dy], end: [x + dx, y + dy], gate, locked });
}
for (const pen of PENS) {
  for (let x = pen.left; x <= pen.right; x++) {
    edge({ x, y: pen.top }, { x, y: pen.top - 1 });
    edge({ x, y: pen.bottom }, { x, y: pen.bottom + 1 }, x === pen.gate);
  }
  for (let y = pen.top; y <= pen.bottom; y++) {
    edge({ x: pen.left, y }, { x: pen.left - 1, y });
    edge({ x: pen.right, y }, { x: pen.right + 1, y });
  }
}
for (let i = 0; i < 20; i++) {
  edge({ x: i, y: 0 }, { x: i, y: -1 });
  edge({ x: i, y: 19 }, { x: i, y: 20 });
  edge({ x: 0, y: i }, { x: -1, y: i });
  edge({ x: 19, y: i }, { x: 20, y: i });
  edge({ x: 14, y: i }, { x: 15, y: i }, false, true);
}
export const FENCES = [...edges.values()];
export function fenceBetween(world, from, to) {
  const fence = edges.get(edgeKey(from, to));
  return fence && !(fence.locked && world.expanded) ? fence : null;
}
export function canCross(world, from, to, animal = false) {
  const fence = fenceBetween(world, from, to);
  return fence === null || (fence.gate && !animal);
}
