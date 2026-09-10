import {ALL_ITEMS,SUBJECT_ITEMS,ITEMS} from "./farm-vocabulary.js";
import {addItem,dropItem,distance,neighbors,reachable,tileAt} from "./farm-world.js";
import {recordGoal} from "./coding-goals.js";

export function resolveItems(actor,instruction) {
  if (instruction.argument!==SUBJECT_ITEMS) return {argument:instruction.argument,amount:instruction.amount};
  if (actor.subject?.kind!=="items") return null;
  return {...actor.subject.value,...(instruction.amount===-1?{}:{amount:instruction.amount})};
}
export const rememberItems=(actor,value)=>{actor.subject={kind:"items",value:structuredClone(value)};};
export const matchesItems=(selection,item)=>selection.argument===ALL_ITEMS || ITEMS[selection.argument].key===item.kind;
export const selectedCount=(items,selection)=>items.filter(item=>matchesItems(selection,item)).reduce((count,item)=>count+item.count,0);

export function transferItems(items,selection,accept) {
  let remaining=selection.amount===-1 ? Infinity : selection.amount, moved=0;
  for (const item of items) {
    if (!matchesItems(selection,item) || remaining===0) continue;
    const count=accept(item,Math.min(item.count,remaining));
    item.count-=count; remaining-=count; moved+=count;
  }
  return moved;
}

export function storageSource(world,point) {
  if (point===world.barn) {
    const items=Object.entries(world.stock).map(([kind,count])=>({kind,count}));
    return {items,commit(){for(const item of items)world.stock[item.kind]=item.count;}};
  }
  if (point===world.compost) {
    const items=[{kind:"fertilizer",count:world.compost.ready}];
    return {items,commit(){world.compost.ready=items[0].count;}};
  }
  if (point===world.cow) {
    const items=[{kind:"milk",count:point.produce}];
    return {items,commit(){point.produce=items[0].count;}};
  }
  if (point.inventory) return {items:point.inventory,commit(){point.inventory=point.inventory.filter(item=>item.count>0);}};
  if (point.items) return {items:point.items,commit(){point.items=point.items.filter(item=>item.count>0);}};
  return null;
}

export function nearbyItemSources(world,actor) {
  return [
    ...[actor,...neighbors(actor)].map(p=>tileAt(world,p.x,p.y)),
    world.barn,world.compost,...(world.cow===null?[]:[world.cow])
  ].filter(point=>reachable(world,actor,point)).sort((a,b)=>distance(actor,a)-distance(actor,b));
}

export function collectItems(world,actor,selection,points) {
  let remaining=selection.amount===-1 && selection.argument!==ALL_ITEMS ? ITEMS[selection.argument].capacity : selection.amount;
  let count=0;
  for (const point of points) {
    const source=storageSource(world,point);
    if (source===null) continue;
    const taken=transferItems(source.items,{...selection,amount:remaining},(item,n)=>{
      const accepted=addItem(actor,item.kind,n,item);
      if (point.items && actor.id!=="player" && item.kind==="eggs") recordGoal(world.coding,"eggs",accepted);
      return accepted;
    });
    source.commit();count+=taken;
    if (remaining!==-1) remaining-=taken;
    if (remaining===0) break;
  }
  return count;
}

export function acceptStored(world,point,item,count) {
  if (point===world.barn) {world.stock[item.kind]+=count;return count;}
  if (point===world.compost) {if(item.kind!=="scraps")return 0;world.compost.input+=count;return count;}
  if (point.inventory) return addItem(point,item.kind,count,item);
  if (point.items) {dropItem(world,point,item.kind,count,item);return count;}
  return 0;
}
