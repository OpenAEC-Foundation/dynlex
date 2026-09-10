import { animals, actors, distance, dropItem, face, neighbors, walkable } from "./farm-world.js";
import { canCross, penAt } from "./farm-boundaries.js";

export function updateLivestock(world) {
  for (const [index, animal] of animals(world).entries()) {
    if (animal.held) continue;
    const cow = animal.id === "cow";
    if (animal.feed > 0 && (cow ? animal.produce < 6 : world.tiles.flat().reduce((sum,tile) => sum + tile.items.filter(i=>i.kind==='eggs').reduce((n,i)=>n+i.count,0),0) < 12)) {
      animal.progress++;
      if (animal.progress >= 20) {
        animal.progress = 0; animal.feed--;
        if (cow) animal.produce++;
        else dropItem(world, animal, "eggs", 1);
      }
    }
    // Pause by a person so feeding and collecting do not become a chase.
    if ((world.tick + index * 3) % (cow ? 7 : 4) !== 0 || actors(world).some(person => distance(person, animal) <= 1)) continue;
    const pen = penAt(animal.x, animal.y);
    const options = neighbors(animal).filter(p => penAt(p.x, p.y) === pen && !(pen && p.x === pen.gate && p.y === pen.bottom)
      && canCross(world, animal, p, true) && walkable(world, p.x, p.y));
    if (options.length === 0) continue;
    const choice = Math.abs(Math.sin(world.tick * 12.9898 + index * 78.233));
    const next = options[Math.floor(choice * options.length)];
    face(animal, next); Object.assign(animal, next);
  }
}
