import { createRoles } from "../../web/farm-roles.js";

// Programs used only by the compiler/simulation integration test. The website
// ships the Lumberjack example; these routines are not served to players.
const sources = {
  gardener: `loop forever:
    walk to the barn
    store everything
    grab a hoe
    grab seeds
    walk to the nearest empty plot
    plow the ground
    plant carrots`,
  waterer: `walk to the barn
store everything
grab a bucket
loop forever:
    if my bucket is empty:
        walk to the pond
        fill my bucket
    if there are thirsty crops:
        walk to the nearest thirsty crop
        water the crops
    else:
        wait`,
  harvester: `loop forever:
    walk to the nearest ripe crop
    harvest the crops
    walk to the barn
    store everything`,
  keeper: `loop forever:
    walk to the barn
    store everything
    grab hay
    grab wheat
    walk to the cow
    feed the cow
    walk to the nearest chicken
    feed the chickens
    walk to the barn
    store everything
    walk to the cow
    collect milk
    walk to the nearest egg
    collect eggs
    walk to the barn
    store everything`,
  composter: `loop forever:
    walk to the barn
    store everything
    grab scraps
    walk to the compost heap
    add scraps to the compost
    if the compost is ready:
        take finished compost from the compost heap
    walk to the barn
    store everything`,
  irrigator: `walk to the barn
store everything
grab a shovel
loop forever:
    walk to the green flag
    dig a channel
    wait`,
  porter: `loop forever:
    walk to the nearest item pile
    pick up nearby items
    walk to the barn
    store everything`
};

export function createTestTeam() {
  return [...createRoles(), ...Object.entries(sources).map(([id, source]) => ({ id, name: id, source, program: null }))];
}
