import { createItem } from "./farm-art.js";

const isTool = kind => ["axe", "hoe", "shovel"].includes(kind);
const wristAngles = (kind, slot, chopping) => {
  if (kind === "axe") return chopping ? [1.2, -Math.PI / 2, 0] : [.4, slot === 0 ? Math.PI : 0, 0];
  if (kind === "hoe") return [.4, Math.PI / 2, 0];
  if (kind === "shovel") return [.18, 0, 0];
  return [0, 0, 0];
};

export function equipFarmer(farmer, inventory) {
  farmer.userData.hands.forEach((hand, slot) => {
    hand.clear();
    if (slot >= inventory.length) return;
    const kind = inventory[slot].kind, item = createItem(kind);
    item.position.set(...item.userData.grip).negate();
    hand.rotation.set(...wristAngles(kind, slot, false));
    hand.add(item);
  });
}

export function poseFarmerArms(farmer, inventory, { time, phase, weight, chopping, dt }) {
  let moving = false;
  const blend = 1 - Math.exp(-dt * 20);
  const ease = (rotation, axis, target) => {
    const difference = Math.atan2(Math.sin(target - rotation[axis]), Math.cos(target - rotation[axis]));
    if (Math.abs(difference) > .001) moving = true;
    rotation[axis] += difference * blend;
  };
  farmer.userData.arms.forEach((arm, slot) => {
    const kind = inventory[slot]?.kind;
    const choppingHand = chopping && kind === "axe";
    const swing = -Math.sin(phase + slot * Math.PI) * (kind ? .12 : .30) * weight;
    ease(arm.rotation, "x", choppingHand ? -1 + Math.sin(time * Math.PI * 4) * .8 : swing);
    ease(arm.rotation, "z", (slot === 0 ? -1 : 1) * (kind ? isTool(kind) ? .22 : .30 : .16));
    const angles = wristAngles(kind, slot, choppingHand);
    ["x", "y", "z"].forEach((axis, index) => ease(arm.userData.hand.rotation, axis, angles[index]));
  });
  return moving;
}
