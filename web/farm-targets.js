import {inArea} from "./farm-areas.js";
import {DESTINATIONS,FLAGS,SUBJECT_TARGET,NO_TARGET} from "./farm-vocabulary.js";
import {actors,ahead,distance,tileAt,unlocked} from "./farm-world.js";

export const standingTree = tile => tile.tree !== null && tile.tree.regrow === 0;
export const thirsty = tile => tile.crop !== null && tile.crop.growth < 1 && tile.moisture < 3;

export function destination(world,actor,key,areaName="") {
  const area=areaName ? world.areas.find(area=>area.name===areaName) : null;
  if (areaName && !area) return null;
  let candidates;
  if (["barn","cow","compost"].includes(key)) candidates=world[key]===null ? [] : [world[key]];
  else if (key==="player") candidates=[world.player];
  else if (key==="worker") candidates=actors(world).filter(other=>other!==actor);
  else if (key==="chicken" || key==="chickens") candidates=world.chickens;
  else if (FLAGS.some(flag=>flag.id===key)) candidates=world.flags.filter(flag=>flag.id===key);
  else if (["ground","frontTree","frontCrop"].includes(key)) {
    const point=ahead(actor), tile=tileAt(world,point.x,point.y);
    candidates=tile && unlocked(world,tile.x,tile.y) && (key==="ground" || (key==="frontTree" ? standingTree(tile) : tile.crop!==null)) ? [tile] : [];
  } else if (key==="sightedTree") {
    candidates=[];
    for (let point=ahead(actor), tile=tileAt(world,point.x,point.y); tile && tile.type!=="pond"; point=ahead({...point,direction:actor.direction}),tile=tileAt(world,point.x,point.y)) {
      if (standingTree(tile)) {candidates=[tile];break;}
    }
  } else candidates=world.tiles.flat().filter(tile=>unlocked(world,tile.x,tile.y) && (
    key==="pond" ? tile.type==="pond" : key==="tree" ? standingTree(tile)
      : key==="plot" ? tile.plot && tile.crop===null : key==="thirsty" ? thirsty(tile)
      : key==="ripe" ? tile.crop!==null && tile.crop.growth>=1 : key==="items" ? tile.items.length>0
      : key==="nearbyItems" ? tile.items.length>0 && distance(actor,tile)<=1
      : key==="egg" ? tile.items.some(item=>item.kind==="eggs") : false
  ));
  return candidates.filter(point=>!area || inArea(area,point)).sort((a,b)=>
    key==="chickens" && distance(actor,a)<=1 && distance(actor,b)<=1 ? a.feed-b.feed : distance(actor,a)-distance(actor,b))[0] ?? null;
}

export function resolveTarget(world,actor,target,area="") {
  if (target===NO_TARGET) return null;
  if (target===SUBJECT_TARGET) return actor.subject?.kind==="target" ? actor.subject.value : null;
  const key=DESTINATIONS[target].key, point=destination(world,actor,key,area);
  if (point===null) return null;
  if (["barn","compost"].includes(key)) return {kind:key};
  if (point.id) return FLAGS.some(flag=>flag.id===point.id)
    ? {kind:"flag",id:point.id,serial:point.serial} : {kind:"actor",id:point.id};
  const position={x:point.x,y:point.y};
  if (["tree","frontTree","sightedTree"].includes(key)) return {kind:"tree",...position,serial:point.tree.serial};
  if (["thirsty","ripe","frontCrop"].includes(key)) return {kind:"crop",...position,serial:point.crop.serial};
  if (["egg","items","nearbyItems"].includes(key)) return {kind:"pile",...position,serials:point.items.filter(item=>key!=="egg" || item.kind==="eggs").map(item=>item.serial)};
  return {kind:"ground",...position};
}

export function targetPoint(world,reference) {
  if (reference===null) return null;
  if (["barn","compost"].includes(reference.kind)) return world[reference.kind];
  if (reference.kind==="actor") return [...actors(world),...world.chickens,...(world.cow===null?[]:[world.cow])].find(actor=>actor.id===reference.id) ?? null;
  if (reference.kind==="flag") return world.flags.find(flag=>flag.id===reference.id && flag.serial===reference.serial) ?? null;
  const tile=tileAt(world,reference.x,reference.y);
  if (reference.kind==="ground") return tile;
  if (reference.kind==="tree") return standingTree(tile) && tile.tree.serial===reference.serial ? tile : null;
  if (reference.kind==="crop") return tile.crop!==null && tile.crop.serial===reference.serial ? tile : null;
  if (reference.kind==="pile") return tile.items.some(item=>reference.serials.includes(item.serial)) ? tile : null;
  throw new Error(`Unknown target reference: ${reference.kind}`);
}

export const rememberTarget = (actor,value) => {actor.subject={kind:"target",value:structuredClone(value)};};
export const targetLabel = instruction => instruction.target===SUBJECT_TARGET ? "it" : DESTINATIONS[instruction.target].phrase+(instruction.area ? ` in ${instruction.area}` : "");
