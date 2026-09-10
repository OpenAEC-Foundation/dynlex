import {recordGoal} from "./coding-goals.js";
import {ACTIONS,CONDITIONS,ITEMS,ROLE_NAMES,itemInfo,ALL_ITEMS,SUBJECT_TARGET,NO_TARGET} from "./farm-vocabulary.js";
import {ahead,addItem,carried,dropItem,face,moveToward,routeTo,neighbors,reachable,removeItem,tileAt,unlocked,walkable} from "./farm-world.js";
import {canCross,penAt} from "./farm-boundaries.js";
import {resolveTarget,targetPoint,targetLabel,rememberTarget,standingTree} from "./farm-targets.js";
import {resolveItems,rememberItems,matchesItems,selectedCount,transferItems,storageSource,acceptStored,nearbyItemSources,collectItems} from "./farm-items.js";
export {destination} from "./farm-targets.js";

const result=(message,done=true,continuation=null)=>({message,done,continuation});
const occupied=(world,tile)=>penAt(tile.x,tile.y) || [world.barn,world.compost].some(point=>point.x===tile.x && point.y===tile.y);
const landReference=reference=>reference!==null && ["ground","tree","crop","pile"].includes(reference.kind);

export function plannedRoute(world,actor,program) {
  let target=actor.target, adjacent=target?.adjacent??true, label="Selected square";
  if (actor.id!=="player") {
    const instruction=program?.[actor.routine.pc];
    if (instruction?.op!=="action" || instruction.key!=="walk") return null;
    const reference=actor.routine.continuation===null ? resolveTarget(world,actor,instruction.target,instruction.area) : actor.routine.continuation.target;
    target=targetPoint(world,reference);label=targetLabel(instruction);
  }
  if (!target) return null;
  return {target,label,path:routeTo(world,actor,target,adjacent)};
}

export function sense(world,actor,instruction) {
  const {key,argument,amount}=instruction, entry=CONDITIONS.find(entry=>entry.key===key);
  const reference=resolveTarget(world,actor,instruction.target,instruction.area), point=targetPoint(world,reference);
  const selection=entry.operands.includes("items") || entry.operands==="stock" ? resolveItems(actor,instruction) : null;
  if (entry.subject==="target") rememberTarget(actor,reference);
  if (entry.subject==="items" && selection!==null) rememberItems(actor,selection);
  if (key==="exists") return point!==null;
  if (key==="reach") return point!==null && reachable(world,actor,point);
  if (key==="rain") return world.raining;
  if (key==="role") return actor.role===ROLE_NAMES[argument];
  if (key==="canStep") {
    const relative=[0,2,3,1][argument], p=ahead({...actor,direction:(actor.direction+relative)%4});
    return walkable(world,p.x,p.y,actor) && canCross(world,actor,p);
  }
  if (["stockAtLeast","stockFewer"].includes(key)) {
    if (selection===null || point===null) return false;
    const storage=storageSource(world,point);
    if (storage===null) return false;
    const count=selectedCount(storage.items,selection);
    return key==="stockAtLeast" ? count>=amount : count<amount;
  }
  if (["full","empty","carrying","notCarrying"].includes(key)) {
    if (selection===null) return false;
    const items=actor.inventory.filter(item=>matchesItems(selection,item));
    const count=selectedCount(items,selection);
    if (key==="carrying" || key==="notCarrying") {
      const carrying=count>0 && count>=(selection.amount===-1?1:selection.amount);
      return key==="carrying" ? carrying : !carrying;
    }
    if (selection.argument===ALL_ITEMS) return key==="full" ? actor.inventory.length===2 : actor.inventory.length===0;
    if (key==="empty") return items.every(item=>item.kind==="bucket" ? item.water===0 : item.count===0);
    return items.length>0 && items.every(item=>item.kind==="bucket" ? item.water===4 : item.count===itemInfo(item.kind).capacity);
  }
  if (point===null) return false;
  if (key==="hungry") return (point.id==="cow" || world.chickens.includes(point)) && point.feed<2;
  if (key==="ready") return point===world.compost ? point.ready>0 : point===world.cow ? point.produce>0 : landReference(reference) && point.crop!==null && point.crop.growth>=1;
  if (!landReference(reference)) return false;
  if (key==="dry") return point.moisture<3;
  if (key==="ripe") return point.crop!==null && point.crop.growth>=1;
  if (key==="bare") return point.crop===null && !standingTree(point) && ["grass","plowed"].includes(point.type);
  if (key==="unplowed") return point.type==="grass" && !standingTree(point);
  throw new Error(`Unknown farm condition: ${key}`);
}

export function perform(world,actor,instruction,continuation=null) {
  const {key,argument}=instruction, entry=ACTIONS.find(entry=>entry.key===key);
  const reference=continuation===null ? resolveTarget(world,actor,instruction.target,instruction.area) : continuation.target;
  const point=targetPoint(world,reference);
  const selection=entry.operands.includes("items") ? resolveItems(actor,instruction) : null;
  if (entry.subject==="target") rememberTarget(actor,reference);
  if (entry.subject==="items" && selection!==null) rememberItems(actor,selection);
  if (key==="focusTarget") return result(point===null?"That target is no longer there.":"Remembering this target.");
  if (key==="focusItems") return result(selection===null?"There is no remembered item selection.":"Remembering these items.");
  if (key==="wait") return result("Waiting.");
  if (key==="turn") {
    actor.direction=(actor.direction+[0,2,3,1][argument])%4;
    return result("Turning.");
  }
  if (key==="step") {
    const p=ahead({...actor,direction:(actor.direction+[0,2,3,1][argument])%4});
    if (!walkable(world,p.x,p.y,actor) || !canCross(world,actor,p)) return result("That square is blocked.");
    Object.assign(actor,p);return result("Stepping.");
  }
  if (key==="walk") {
    if (point===null) return result(instruction.target===SUBJECT_TARGET || continuation!==null ? "That target is no longer there." : `No ${targetLabel(instruction)} to walk to.`);
    const moved=moveToward(world,actor,point);
    return result(moved.done?`At ${targetLabel(instruction)}.`:moved.message,moved.done,{target:reference});
  }
  if (entry.operands.includes("items") && selection===null) return result("There is no remembered item selection.");
  if (["store","add","give"].includes(key)) {
    if (point===null || !reachable(world,actor,point)) return result("Walk beside the chosen storage or person first.");
    if (key==="give" && (!point.inventory || point===actor)) return result("Choose another person to give items to.");
    const count=transferItems(actor.inventory,selection,(item,count)=>acceptStored(world,point,item,count));
    actor.inventory=actor.inventory.filter(item=>item.count>0);
    return result(count?`Transferred ${count} item${count===1?"":"s"}.`:"No selected items can be transferred here.");
  }
  if (key==="drop") {
    const count=transferItems(actor.inventory,selection,(item,count)=>{dropItem(world,actor,item.kind,count,item);return count;});
    actor.inventory=actor.inventory.filter(item=>item.count>0);
    return result(count?`Put down ${count} item${count===1?"":"s"}.`:"None of those items to put down.");
  }
  if (key==="collect") {
    const explicit=instruction.target!==NO_TARGET;
    if (explicit && point===null) return result("That source is no longer there.");
    if (explicit && !reachable(world,actor,point)) return result("That source is out of reach. Walk closer first.");
    if (point===actor) return result("Those items are already in my hands.");
    const count=collectItems(world,actor,selection,explicit?[point]:nearbyItemSources(world,actor));
    return result(count?`Collected ${count} item${count===1?"":"s"}.`:"No selected items I can carry nearby.");
  }
  if (key==="fill") {
    if (selection.argument!==ALL_ITEMS && ITEMS[selection.argument].key!=="bucket") return result("Only buckets can be filled with water.");
    if (![actor,...neighbors(actor)].some(p=>world.tiles[p.y][p.x].water && canCross(world,actor,p))) return result("Walk beside the pond or a flowing channel.");
    let count=0;
    for (const item of actor.inventory) if (item.kind==="bucket" && item.water<4 && (selection.amount===-1 || count<selection.amount)) {item.water=4;count++;}
    return result(count?`Filled ${count} bucket${count===1?"":"s"}.`:"No empty bucket to fill.");
  }
  if (point===null) return result("That target is no longer there.");
  if (!reachable(world,actor,point)) return result("That target is out of reach. Walk closer first.");
  face(actor,point);
  if (key==="feed") {
    if (point!==world.cow && !world.chickens.includes(point)) return result("Choose an animal to feed.");
    const food=point===world.cow?"hay":"wheat";
    if (point.feed>=4) return result("Already well fed.");
    if (!removeItem(actor,food)) return result(`I need ${food}.`);
    point.feed++;return result("Fed the animal.");
  }
  if (!landReference(reference) || !unlocked(world,point.x,point.y)) return result("Choose available ground for this job.");
  const tile=point;
  if (["plow","dig","plant"].includes(key) && occupied(world,tile)) return result("That square is occupied. Choose clear ground.");
  if (key==="chop") {
    if (!carried(actor,"axe")) return result("I need an axe.");
    if (!standingTree(tile)) return result("Choose a standing tree to chop.");
    tile.tree.hits--;
    if (tile.tree.hits>0) return result("Chopping…",false,{target:reference});
    tile.tree.regrow=180;dropItem(world,tile,"logs",2);return result("Timber! Two logs are ready to collect.");
  }
  if (key==="plow") {
    if (!carried(actor,"hoe")) return result("I need a hoe.");
    if (tile.type==="plowed") return result("Already plowed.");
    if (tile.type!=="grass" || tile.tree!==null) return result("This ground cannot be plowed.");
    Object.assign(tile,{type:"plowed",plot:true});return result("Plowed a new planting bed.");
  }
  if (key==="plant") {
    if (selection.argument===ALL_ITEMS || !["carrots","wheat"].includes(ITEMS[selection.argument].key)) return result("Plant carrots or wheat.");
    if (selection.amount===0) return result("No crops requested.");
    if (tile.type!=="plowed" || tile.crop!==null) return result("Choose an empty, plowed bed.");
    if (!removeItem(actor,"seeds")) return result("I need seeds from the barn.");
    const kind=ITEMS[selection.argument].key;tile.crop={serial:world.nextEntity++,kind,growth:0};
    if (actor.id!=="player") recordGoal(world.coding,"planting",1,`${tile.x},${tile.y}`);
    return result(`Planted ${kind}.`);
  }
  if (key==="water") {
    const bucket=actor.inventory.find(item=>item.kind==="bucket" && item.water>0);
    if (!bucket) return result("I need a bucket filled at the pond.");
    if (tile.type!=="plowed") return result("Choose a planting bed.");
    bucket.water--;
    if (actor.id!=="player" && tile.crop!==null && tile.moisture<10) recordGoal(world.coding,"watering",1,`${tile.x},${tile.y}`);
    tile.moisture=10;return result("Watered this bed.");
  }
  if (key==="harvest") {
    if (tile.crop===null || tile.crop.growth<1) return result("These crops aren't ripe yet.");
    const kind=tile.crop.kind;
    for (const [item,count] of [[kind,2],["scraps",1],...(kind==="wheat"?[["hay",1]]:[])]) dropItem(world,tile,item,count-addItem(actor,item,count));
    world.stock.seeds+=2;tile.crop=null;tile.fertilizer=0;return result("Harvested crops and scraps. Saved two seeds in the barn.");
  }
  if (key==="fertilize") {
    if (tile.type!=="plowed") return result("Choose a planting bed.");
    if (!removeItem(actor,"fertilizer")) return result("I need finished compost.");
    tile.fertilizer=1;return result("Fertilized this bed: crops grow twice as fast.");
  }
  if (key==="dig") {
    if (!carried(actor,"shovel")) return result("I need a shovel.");
    if (tile.type==="channel") return result("A channel is already here.");
    if (tile.type!=="grass" || tile.tree!==null || tile.crop!==null) return result("Dig channels through clear grass.");
    tile.type="channel";return result("Dug a channel. Connect it to the pond for water.");
  }
  throw new Error(`Unknown farm action: ${key}`);
}
