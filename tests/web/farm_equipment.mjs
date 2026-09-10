import assert from "node:assert/strict";
import { Vector3 } from "../../web/vendor/three/three.module.min.js";
import { createFarmer } from "../../web/farm-characters.js";
import { equipFarmer, poseFarmerArms } from "../../web/farm-equipment.js";
import { ITEMS } from "../../web/farm-vocabulary.js";

const farmer = createFarmer();
farmer.position.set(3, .4, -2); farmer.rotation.y = .9;
function assertGrips() {
  farmer.updateMatrixWorld(true);
  for (const hand of farmer.userData.hands) for (const item of hand.children) {
    const grip = item.localToWorld(new Vector3(...item.userData.grip));
    assert.ok(grip.distanceTo(hand.getWorldPosition(new Vector3())) < 1e-8,
      `${item.name} must stay in the hand as the arm moves`);
  }
}
for (const { key } of ITEMS) {
  const inventory = [{kind:key}, {kind:key}];
  equipFarmer(farmer, inventory);
  for (let frame = 0; frame < 60; frame++) {
    poseFarmerArms(farmer, inventory, {time:frame / 60, phase:frame * .2, weight:1, chopping:false, dt:1 / 60});
    assertGrips();
  }
}
for (const slot of [0, 1]) {
  const inventory = slot ? [{kind:"bucket"}, {kind:"axe"}] : [{kind:"axe"}, {kind:"logs"}];
  equipFarmer(farmer, inventory);
  const angles = [];
  for (let frame = 0; frame < 60; frame++) {
    poseFarmerArms(farmer, inventory, {time:frame / 60, phase:0, weight:0, chopping:true, dt:1 / 60});
    assertGrips(); angles.push(farmer.userData.arms[slot].rotation.x);
  }
  assert.ok(Math.max(...angles) - Math.min(...angles) > .5, "The arm holding the axe must perform the chop");
}
equipFarmer(farmer, []);
assert.ok(farmer.userData.hands.every(hand => hand.children.length === 0), "Depositing items clears both hands");
console.log("Every carried item stays at its grip while walking; either hand can chop; depositing clears the equipment.");
