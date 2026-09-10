import {action,condition,itemId,targetId} from "./farm-vocabulary.js";

// Version 5 used literal pronouns and individual opcodes for each fixed phrase.
// Convert that data once, before the current interpreter or editor sees it.
export function upgradeFarmSource(source) {
  const replacements=[
    [/\bi can reach it\b/g,"i can reach the tree in front of me"],
    [/\bchop it down\b/g,"chop the tree in front of me down"],
    [/\btake finished compost\b/g,"take finished compost from the compost heap"]
  ];
  return source.replace(/"(?:\\[\s\S]|[^"\\])*"|#[^\r\n]*|[^"#]+/g,chunk=>{
    if (chunk.startsWith('"') || chunk.startsWith("#")) return chunk;
    for (const [pattern,replacement] of replacements) chunk=chunk.replace(pattern,replacement);
    return chunk;
  });
}

function upgradeInstruction(old) {
  if (!["action","test"].includes(old.op)) return {...old,range:null};
  const target=key=>({target:targetId(key)}), items=key=>({argument:itemId(key)});
  const actions={
    forward:["step",{argument:0}],backward:["step",{argument:1}],left:["turn",{argument:2}],right:["turn",{argument:3}],
    chop:["chop",target("frontTree")],plow:["plow",target("ground")],carrots:["plant",items("carrots")],wheat:["plant",items("wheat")],
    water:["water"],harvest:["harvest"],fill:["fill",{...items("bucket"),amount:1}],store:["store"],pickup:["collect"],dig:["dig"],
    feedCow:["feed",target("cow")],feedChickens:["feed",target("chickens")],milk:["collect",items("milk")],eggs:["collect",items("eggs")],
    compost:["add"],takeCompost:["collect",{...items("fertilizer"),...target("compost")}],fertilize:["fertilize"],wait:["wait"],
    grab:["collect",{argument:old.argument}],give:["give",{argument:old.argument}],walk:["walk",{target:old.argument,area:old.area}],drop:["drop"]
  };
  const conditions={
    treeAhead:["exists",target("sightedTree")],reachTree:["reach",target("frontTree")],full:["full"],empty:["empty"],rain:["rain"],
    dry:["dry",target("ground")],ripe:["ripe",target("frontCrop")],bare:["bare",target("ground")],unplowed:["unplowed",target("ground")],
    bucketEmpty:["empty",{...items("bucket"),amount:1}],thirsty:["exists",target("thirsty")],items:["exists",target("nearbyItems")],
    compostReady:["ready",target("compost")],cowHungry:["hungry",target("cow")],chickensHungry:["hungry",target("chickens")],
    carrying:["carrying",{argument:old.argument}],notCarrying:["notCarrying",{argument:old.argument}],role:["role",{argument:old.argument}],
    canStep:["canStep",{argument:0}],barnAtLeast:["stockAtLeast",{argument:old.argument,amount:old.amount}],barnFewer:["stockFewer",{argument:old.argument,amount:old.amount}]
  };
  const [key,values]=(old.op==="action"?actions:conditions)[old.key];
  const next=(old.op==="action"?action:condition)(key,values);
  if (old.op==="test") next.to=old.to;
  return next;
}

export function upgradeFarmLanguage(data) {
  const {world,roles}=data;
  world.nextEntity=1;
  for (const tile of world.tiles.flat()) {
    for (const entity of [tile.tree,tile.crop,...tile.items]) if (entity!==null) entity.serial=world.nextEntity++;
  }
  for (const flag of world.flags) flag.serial=world.nextEntity++;
  for (const actor of [world.player,...world.workers]) {
    actor.subject=null;actor.routine.continuation=null;actor.routine.trace=null;
  }
  for (const role of roles) {
    role.source=upgradeFarmSource(role.source);role.applied=upgradeFarmSource(role.applied);
    role.needsRebuild=role.program!==null;
    delete role.needsSourceMap;
    if (role.program!==null) role.program=role.program.map(upgradeInstruction);
  }
}

// Version 6 split collection by verb. Recompile the unchanged source once so
// omitted sources search nearby and explicit `from` arguments remain explicit.
export function upgradeFarmCollection(data) {
  for (const role of data.roles) {
    if (role.program===null) continue;
    for (const instruction of role.program) {
      if (instruction.op==="action" && instruction.key==="take") instruction.key="collect";
    }
    role.needsRebuild=true;
  }
}
