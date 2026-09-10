export const FLAGS = [
  ["red", "#cd6350"], ["blue", "#609eaf"], ["green", "#8aab5b"],
  ["yellow", "#edd05b"], ["purple", "#956ab9"], ["orange", "#e38b3d"],
  ["pink", "#df95b5"], ["white", "#f0e9d5"], ["black", "#343a38"]
].map(([id, color]) => ({ id, color }));

export const ITEMS = [
  ["axe", "an axe", "Axe", 1, ["axes"]], ["hoe", "a hoe", "Hoe", 1, ["hoes"]], ["bucket", "a bucket", "Bucket", 1, ["buckets"]],
  ["shovel", "a shovel", "Shovel", 1, ["shovels"]], ["seeds", "seeds", "Seeds", 6], ["logs", "logs", "Logs", 3],
  ["carrots", "carrots", "Carrots", 6], ["wheat", "wheat", "Wheat", 6], ["hay", "hay", "Hay", 6],
  ["eggs", "eggs", "Eggs", 6], ["milk", "milk", "Milk", 6], ["scraps", "scraps", "Scraps", 6],
  ["fertilizer", "fertilizer", "Fertilizer", 6]
].map(([key, phrase, label, capacity, aliases = []], id) => ({ id, key, phrase, label, capacity, aliases }));

export const DESTINATIONS = [
  ["barn", "the barn", "barn"], ["pond", "the pond", "pond"], ["red", "the red flag", "red flag"], ["blue", "the blue flag", "blue flag"],
  ["green", "the green flag", "green flag"], ["tree", null, "tree"], ["plot", null, "empty plot"],
  ["thirsty", null, "thirsty crop"], ["ripe", null, "ripe crop"],
  ["cow", "the cow", "cow"], ["chicken", null, "chicken"], ["compost", "the compost heap", "compost heap"], ["items", null, "item pile"],
  ["egg", null, "egg"],
  ...FLAGS.slice(3).map(flag => [flag.id, `the ${flag.id} flag`, `${flag.id} flag`]),
  ["ground", "the ground"], ["frontCrop", "the crops"], ["frontTree", "the tree in front of me"],
  ["sightedTree", "a tree in front of me"], ["chickens", "the chickens"],
  ["nearbyItems", "items nearby"], ["worker", "a nearby worker", "worker"], ["player", "the farmer", "farmer"]
].map(([key, literal, kind = null], id) => ({ id, key, literal, kind, phrase: literal ?? `the nearest ${kind}` }));

export const SUBJECT_TARGET = -1, NO_TARGET = -2, ALL_ITEMS = -1, SUBJECT_ITEMS = -2;
export const DIRECTIONS = ["forward", "backward", "left", "right"];
export const FEATURES = ["a channel"];
export const ROLE_NAMES = ["lumberjack", "gardener", "waterer", "harvester", "keeper", "composter", "irrigator", "porter"];
export const targetId = key => DESTINATIONS.find(entry => entry.key === key).id;
export const itemId = key => ITEMS.find(entry => entry.key === key).id;
export const ITEM_ALIASES = [
  ["everything",ALL_ITEMS], ["nearby items",ALL_ITEMS], ["my hands",ALL_ITEMS],
  ["my bucket",itemId("bucket")], ["finished compost",itemId("fertilizer")]
];
export const TARGET_ALIASES = [
  ["the soil","ground"], ["the soil in front of me","ground"], ["the ground in front of me","ground"],
  ["the crops in front of me","frontCrop"], ["the compost","compost"], ["thirsty crops","thirsty"]
];

const definition = (key, patterns, operands, defaults, subject = null) => ({key, patterns, operands, defaults, subject});
export const ACTIONS = [
  definition("step", ["step {farm direction:direction}"], "direction", {argument:0}),
  definition("turn", ["turn {farm direction:direction}"], "direction", {argument:2}),
  definition("walk", ["walk to {farm target:target}"], "target", {target:targetId("barn")}, "target"),
  definition("chop", ["chop {farm target:target} down"], "target", {target:targetId("frontTree")}, "target"),
  definition("plow", ["plow {farm target:target}"], "target", {target:targetId("ground")}, "target"),
  definition("plant", ["plant {farm item:items}", "plant {farm item:items} in {farm target:target}"], "items-target", {argument:itemId("carrots"),target:targetId("ground")}, "items"),
  definition("water", ["water {farm target:target}"], "target", {target:targetId("frontCrop")}, "target"),
  definition("harvest", ["harvest {farm target:target}"], "target", {target:targetId("frontCrop")}, "target"),
  definition("fertilize", ["fertilize {farm target:target}"], "target", {target:targetId("ground")}, "target"),
  definition("fill", ["fill {farm item:items}"], "items", {argument:itemId("bucket")}, "items"),
  definition("store", ["store {farm item:items}"], "items", {argument:ALL_ITEMS,target:targetId("barn")}, "items"),
  definition("give", ["give {farm item:items} to {farm target:target}"], "items-target", {argument:itemId("logs"),target:targetId("worker")}, "items"),
  definition("drop", ["put down {farm item:items}"], "items", {argument:ALL_ITEMS}, "items"),
  definition("collect", ["[grab|take|collect|pick up] {farm item:items}", "[grab|take|collect|pick up] {farm item:items} from {farm target:target}"], "items-target", {argument:ALL_ITEMS}, "items"),
  definition("dig", ["dig {farm feature:feature}", "dig {farm feature:feature} in {farm target:target}"], "feature-target", {argument:0,target:targetId("ground")}),
  definition("feed", ["feed {farm target:target}"], "target", {target:targetId("cow")}, "target"),
  definition("add", ["add {farm item:items} to {farm target:target}"], "items-target", {argument:itemId("scraps"),target:targetId("compost")}, "items"),
  definition("wait", ["wait"], "none", {}),
  definition("focusTarget", ["set the subject to {farm target:target}"], "target", {}, "target"),
  definition("focusItems", ["set the subject to {farm item:items}"], "items", {argument:ALL_ITEMS}, "items")
].map((entry,id) => ({...entry,id}));

export const CONDITIONS = [
  definition("exists", ["there [is|are] {farm target:target}", "there's {farm target:target}", "{farm target:target} [is|are] [still|] there", "{farm target:target}'s still there"], "target", {}, "target"),
  definition("reach", ["i can reach {farm target:target}"], "target", {}, "target"),
  definition("full", ["{farm item:items} [is|are] full"], "items", {argument:ALL_ITEMS}, "items"),
  definition("empty", ["{farm item:items} [is|are] empty"], "items", {argument:ALL_ITEMS}, "items"),
  definition("rain", ["it's raining"], "none", {}),
  definition("dry", ["{farm target:target} [is|are] dry"], "target", {}, "target"),
  definition("ripe", ["{farm target:target} [is|are] ripe"], "target", {}, "target"),
  definition("bare", ["{farm target:target} [is|are] empty"], "target", {}, "target"),
  definition("unplowed", ["{farm target:target} [needs|need] plowing"], "target", {}, "target"),
  definition("ready", ["{farm target:target} [is|are] ready"], "target", {}, "target"),
  definition("hungry", ["{farm target:target} [is|are] hungry"], "target", {}, "target"),
  definition("carrying", ["i am carrying {farm item:items}"], "items", {}, "items"),
  definition("notCarrying", ["i am not carrying {farm item:items}"], "items", {}, "items"),
  definition("role", ["i am a {farm role:role}"], "role", {}),
  definition("canStep", ["i can step {farm direction:direction}"], "direction", {argument:0}),
  definition("stockAtLeast", ["there are at least {a farm number:count} {farm item:items} in {farm target:target}"], "stock", {target:targetId("barn")}),
  definition("stockFewer", ["there are fewer than {a farm number:count} {farm item:items} in {farm target:target}"], "stock", {target:targetId("barn")})
].map((entry,id) => ({...entry,id}));

export const itemInfo = key => ITEMS.find(item => item.key === key);
export const actionInfo = key => ACTIONS.find(action => action.key === key);
export function instruction(op, key, values = {}) {
  const entry = (op === "action" ? ACTIONS : CONDITIONS).find(candidate => candidate.key === key);
  return {op,key,argument:0,amount:-1,target:NO_TARGET,area:"",range:null,...entry.defaults,...values};
}
export const action = (key, values) => instruction("action",key,values);
export const condition = (key, values) => instruction("test",key,values);

export function example(entry,values={}) {
  const instruction={argument:0,amount:-1,target:SUBJECT_TARGET,...entry.defaults,...values};
  let items=instruction.argument===ALL_ITEMS ? "everything" : instruction.argument===SUBJECT_ITEMS ? "it" : ITEMS[instruction.argument]?.phrase;
  if (instruction.amount>=0) items=`${instruction.amount} ${items}`;
  const words={items,target:instruction.target<0?"it":DESTINATIONS[instruction.target].phrase,
    direction:DIRECTIONS[instruction.argument],feature:FEATURES[instruction.argument],role:ROLE_NAMES[instruction.argument],count:"2"};
  const pattern=entry.patterns.find(pattern=>values.target===undefined || pattern.includes(":target}"));
  return pattern.replace(/\{[^:}]+:([^}]+)\}/g,(_,name)=>words[name]).replace(/\[([^|\]]+)[^\]]*\]/g,"$1");
}

export const MANUAL_ACTIONS = [
  ["chop","chop"], ["plow","plow"], ["carrots","plant"], ["wheat","plant",{argument:itemId("wheat")}],
  ["water","water"], ["harvest","harvest"], ["fill","fill"], ["pickup","collect"], ["dig","dig"],
  ["feedCow","feed"], ["feedChickens","feed",{target:targetId("chickens")}],
  ["milk","collect",{argument:itemId("milk")}], ["eggs","collect",{argument:itemId("eggs")}],
  ["compost","add"], ["takeCompost","collect",{argument:itemId("fertilizer"),target:targetId("compost")}],
  ["fertilize","fertilize"], ["store","store"], ["drop","drop"],
  ["left","turn",{argument:2}], ["right","turn",{argument:3}]
].map(([id,key,values])=>({id,instruction:action(key,values),label:example(actionInfo(key),values)}));
