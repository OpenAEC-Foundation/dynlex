import * as THREE from "./vendor/three/three.module.min.js";

const shadow = (mesh) => {
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  return mesh;
};

const standard = (color, options = {}) => new THREE.MeshStandardMaterial({
  color,
  roughness: .78,
  metalness: 0,
  ...options
});

const MAT = Object.freeze({
  cream: standard(0xeee5ca),
  cowBlack: standard(0x282722),
  cowNose: standard(0xc89586, { roughness: .9 }),
  dark: standard(0x292821),
  eye: standard(0xf8f2df),
  pupil: standard(0x17130f, { roughness: .35 }),
  highlight: standard(0xffffff, { emissive: 0xffffff, emissiveIntensity: .2 }),
  horn: standard(0xd8c894),
  leather: standard(0x8d4730),
  brass: standard(0xc99938, { metalness: .55, roughness: .35 }),
  denim: standard(0x355d76),
  shirt: standard(0xc77a43),
  skin: standard(0xb9774e),
  hair: standard(0x513722),
  straw: standard(0xd2a54c),
  boot: standard(0x493526),
  trunk: standard(0x5c3e27),
  pine: standard(0x285c3b),
  pineLight: standard(0x3d7548),
  leaf: standard(0x3f7842),
  leafLight: standard(0x61934e),
  wall: standard(0xd9bd87),
  plaster: standard(0xead9af),
  roof: standard(0x9d3f2f),
  roofDark: standard(0x723026),
  window: standard(0x83bdd0, { roughness: .25, metalness: .05 }),
  frame: standard(0xf1e2bb),
  door: standard(0x5b3926),
  stone: standard(0x77766b)
});

// Coat markings are shaded on the body itself, so they follow its curvature.
const cowCoat = MAT.cream.clone();
cowCoat.onBeforeCompile = (shader) => {
  shader.vertexShader = `varying vec3 cowPosition;\n${shader.vertexShader}`.replace(
    "#include <begin_vertex>", "#include <begin_vertex>\ncowPosition = position;"
  );
  shader.fragmentShader = `varying vec3 cowPosition;\n${shader.fragmentShader}`.replace(
    "#include <color_fragment>",
    `#include <color_fragment>
     vec3 p = normalize(cowPosition);
     vec2 leftPatch = vec2((p.y - .13) / .56, (p.z + .28) / .58);
     vec2 rightPatch = vec2((p.y + .04) / .49, (p.z - .19) / .52);
     vec2 backPatch = vec2((p.x + .3) / .54, (p.z + .47) / .55);
     float leftInk = step(p.x, -.45) * (1.0 - smoothstep(.90, 1.02, dot(leftPatch, leftPatch) + .13 * sin(p.y * 18.0 + p.z * 8.0)));
     float rightInk = step(.50, p.x) * (1.0 - smoothstep(.90, 1.02, dot(rightPatch, rightPatch) + .12 * sin(p.z * 19.0)));
     float backInk = step(.45, p.y) * (1.0 - smoothstep(.90, 1.02, dot(backPatch, backPatch)));
     diffuseColor.rgb = mix(diffuseColor.rgb, vec3(.021, .020, .016), max(leftInk, max(rightInk, backInk)));`
  );
};

const GEO = Object.freeze({
  sphere: new THREE.SphereGeometry(1, 16, 12),
  lowSphere: new THREE.SphereGeometry(1, 10, 8),
  box: new THREE.BoxGeometry(1, 1, 1),
  cylinder: new THREE.CylinderGeometry(1, 1, 1, 10),
  cone: new THREE.ConeGeometry(1, 1, 10),
  torus: new THREE.TorusGeometry(1, .24, 7, 12)
});

function part(geometry, material, position, scale, rotation = null) {
  const mesh = shadow(new THREE.Mesh(geometry, material));
  mesh.position.set(...position);
  mesh.scale.set(...scale);
  if (rotation) mesh.rotation.set(...rotation);
  return mesh;
}

function add(group, geometry, material, position, scale, rotation = null) {
  const mesh = part(geometry, material, position, scale, rotation);
  group.add(mesh);
  return mesh;
}

function cowLeg(x, z) {
  const pivot = new THREE.Group();
  pivot.position.set(x, .49, z);
  add(pivot, GEO.cylinder, MAT.cream, [0, -.13, 0], [.055, .26, .055]);
  add(pivot, GEO.cylinder, MAT.cowBlack, [0, -.34, .006], [.062, .17, .062]);
  add(pivot, GEO.box, MAT.dark, [0, -.445, .025], [.13, .08, .17]);
  return pivot;
}

function cowEye(x) {
  const eye = new THREE.Group();
  eye.position.set(x, .69, .428);
  const white = add(eye, GEO.sphere, MAT.eye, [0, 0, 0], [.061, .052, .025]);
  white.rotation.x = -.08;
  add(eye, GEO.sphere, MAT.pupil, [0, -.003, .023], [.027, .031, .013]);
  add(eye, GEO.sphere, MAT.highlight, [-.009, .012, .034], [.008, .008, .005]);
  return eye;
}

export function createCow() {
  const cow = new THREE.Group();
  cow.name = "Bessie";

  add(cow, GEO.sphere, cowCoat, [0, .51, 0], [.28, .29, .36]);
  add(cow, GEO.sphere, MAT.cream, [0, .67, .27], [.21, .205, .16], [-.08, 0, 0]);
  add(cow, GEO.sphere, MAT.cowNose, [0, .59, .405], [.17, .115, .08]);
  add(cow, GEO.sphere, MAT.cowBlack, [.11, .69, .426], [.085, .075, .008], [-.08, 0, 0]);

  for (const x of [-.076, .076]) {
    add(cow, GEO.sphere, MAT.dark, [x, .607, .489], [.018, .012, .009]);
  }
  cow.add(cowEye(-.105), cowEye(.105));

  for (const x of [-.17, .17]) {
    add(cow, GEO.sphere, MAT.cream, [x, .72, .31], [.12, .045, .072], [0, 0, x < 0 ? -.25 : .25]);
    add(cow, GEO.cone, MAT.horn, [x * .7, .855, .29], [.035, .12, .035], [x < 0 ? -.22 : .22, 0, x < 0 ? .2 : -.2]);
  }

  const legs = [
    cowLeg(-.18, -.2), cowLeg(.18, -.2), cowLeg(-.18, .19), cowLeg(.18, .19)
  ];
  cow.add(...legs);
  cow.userData.legs = legs;

  const tail = new THREE.Group();
  tail.position.set(0, .64, -.32);
  const tailStem = add(tail, GEO.cylinder, MAT.cream, [0, -.11, -.02], [.025, .19, .025], [.45, 0, 0]);
  tailStem.rotation.z = -.08;
  add(tail, GEO.sphere, MAT.cowBlack, [0, -.22, -.1], [.05, .07, .05]);
  cow.add(tail);
  cow.userData.tail = tail;

  add(cow, GEO.torus, MAT.leather, [0, .64, .29], [.18, .18, .18], [Math.PI / 2, 0, 0]);
  add(cow, GEO.sphere, MAT.brass, [0, .45, .34], [.05, .065, .035]);
  cow.scale.setScalar(.93);
  return cow;
}

function farmerLeg(x) {
  const leg = new THREE.Group();
  leg.position.set(x, .41, 0);
  add(leg, GEO.cylinder, MAT.denim, [0, -.15, 0], [.07, .3, .07]);
  add(leg, GEO.box, MAT.boot, [0, -.335, .035], [.16, .1, .22]);
  return leg;
}

function farmerArm(x) {
  const arm = new THREE.Group();
  arm.position.set(x, .72, 0);
  arm.rotation.z = x < 0 ? -.16 : .16;
  add(arm, GEO.cylinder, MAT.shirt, [0, -.13, 0], [.065, .25, .065]);
  add(arm, GEO.sphere, MAT.skin, [0, -.285, 0], [.075, .08, .075]);
  const hand = new THREE.Group();
  hand.position.set(0, -.285, .045);
  arm.add(hand);
  arm.userData.hand = hand;
  return arm;
}

export function createFarmer(shirtColor = null) {
  const farmer = new THREE.Group();
  farmer.name = "Farmer";
  const legs = [farmerLeg(-.09), farmerLeg(.09)];
  farmer.add(...legs);
  farmer.userData.legs = legs;
  add(farmer, GEO.box, MAT.shirt, [0, .61, 0], [.35, .34, .23]);
  add(farmer, GEO.box, MAT.denim, [0, .59, .125], [.23, .31, .025]);
  add(farmer, GEO.box, MAT.denim, [-.145, .67, .13], [.045, .26, .025], [0, 0, -.12]);
  add(farmer, GEO.box, MAT.denim, [.145, .67, .13], [.045, .26, .025], [0, 0, .12]);
  const arms = [farmerArm(-.225), farmerArm(.225)];
  farmer.add(...arms);
  farmer.userData.arms = arms;
  farmer.userData.hands = arms.map(arm => arm.userData.hand);
  add(farmer, GEO.sphere, MAT.skin, [0, .87, .015], [.145, .155, .135]);
  add(farmer, GEO.sphere, MAT.hair, [0, .93, -.075], [.15, .105, .08]);
  for (const x of [-.052, .052]) {
    add(farmer, GEO.sphere, MAT.eye, [x, .895, .137], [.024, .021, .01]);
    add(farmer, GEO.sphere, MAT.pupil, [x, .893, .146], [.011, .012, .006]);
  }
  add(farmer, GEO.cylinder, MAT.straw, [0, 1.02, 0], [.24, .035, .24]);
  add(farmer, GEO.cylinder, MAT.straw, [0, 1.07, 0], [.15, .1, .15]);
  add(farmer, GEO.box, MAT.leather, [0, 1.04, .145], [.31, .025, .018]);
  if (shirtColor !== null) {
    const shirt = MAT.shirt.clone();
    shirt.color.set(shirtColor);
    farmer.traverse(object => { if (object.material === MAT.shirt) object.material = shirt; });
  }
  farmer.scale.setScalar(.88);
  return farmer;
}

function seeded(seed) {
  let state = (Number(seed) || 1) >>> 0;
  return () => {
    state = (state * 1664525 + 1013904223) >>> 0;
    return state / 0x100000000;
  };
}

export function createTree(seed = 1) {
  const random = seeded(seed);
  const tree = new THREE.Group();
  const height = 1.1 + random() * .7;
  const broadleaf = random() > .48;
  add(tree, GEO.cylinder, MAT.trunk, [0, height * .31, 0], [.065 + random() * .025, height * .62, .065 + random() * .025]);
  if (broadleaf) {
    const crownY = height - .36;
    for (let index = 0; index < 6; index += 1) {
      const angle = index * Math.PI * 2 / 6 + random() * .35;
      const radius = .12 + random() * .17;
      add(
        tree,
        GEO.lowSphere,
        index % 2 ? MAT.leafLight : MAT.leaf,
        [Math.cos(angle) * radius, crownY + (random() - .3) * .24, Math.sin(angle) * radius],
        [.25 + random() * .1, .24 + random() * .14, .25 + random() * .1]
      );
    }
    add(tree, GEO.lowSphere, MAT.leaf, [0, crownY + .18, 0], [.31, .32, .31]);
  } else {
    for (let tier = 0; tier < 4; tier += 1) {
      const width = .42 - tier * .065 + random() * .035;
      add(tree, GEO.cone, tier % 2 ? MAT.pineLight : MAT.pine, [0, height * (.48 + tier * .13), 0], [width, .48, width]);
    }
  }
  tree.userData.height = height;
  return tree;
}

function windowUnit(position, rotationY = 0) {
  const unit = new THREE.Group();
  unit.position.set(...position);
  unit.rotation.y = rotationY;
  add(unit, GEO.box, MAT.frame, [0, 0, 0], [.25, .27, .035]);
  add(unit, GEO.box, MAT.window, [0, 0, .022], [.2, .22, .025]);
  add(unit, GEO.box, MAT.frame, [0, 0, .042], [.018, .23, .018]);
  add(unit, GEO.box, MAT.frame, [0, 0, .043], [.21, .018, .018]);
  return unit;
}

export function createHouse() {
  const house = new THREE.Group();
  house.name = "Home";
  add(house, GEO.box, MAT.wall, [0, .42, 0], [.86, .78, .76]);
  add(house, GEO.box, MAT.plaster, [0, .43, .386], [.82, .7, .025]);
  // The gable follows the wall footprint; two solid sloping panels meet at the
  // ridge. Build in house coordinates so there are no coupled Euler rotations.
  const gable = new THREE.Shape();
  gable.moveTo(-.43, 0); gable.lineTo(.43, 0); gable.lineTo(0, .37); gable.closePath();
  add(house, new THREE.ExtrudeGeometry(gable, { depth: .76, bevelEnabled: false }), MAT.plaster,
    [0, .81, -.38], [1, 1, 1]);
  const roofAngle = Math.atan2(.43, .52);
  const roofLength = Math.hypot(.43, .52);
  for (const side of [-1, 1]) {
    add(house, GEO.box, MAT.roof, [side * .26, .995, 0], [roofLength, .055, .96], [0, 0, -side * roofAngle]);
    add(house, GEO.box, MAT.roofDark, [side * .515, .785, 0], [.06, .075, .99]);
    // Raised seams and fascia give the roof readable edges from every angle.
    for (const z of [-.46, -.23, 0, .23, .46]) add(house, GEO.box, MAT.roofDark,
      [side * .26, 1.028, z], [roofLength, .012, .014], [0, 0, -side * roofAngle]);
  }
  add(house, GEO.box, MAT.roofDark, [0, 1.217, 0], [.075, .05, .99]);
  add(house, GEO.box, MAT.door, [0, .31, .405], [.23, .55, .055]);
  add(house, GEO.sphere, MAT.brass, [.075, .32, .447], [.018, .018, .012]);
  house.add(windowUnit([-.27, .53, .408]), windowUnit([.27, .53, .408]));
  add(house, GEO.box, MAT.roofDark, [.26, 1.22, -.12], [.15, .44, .15]);
  add(house, GEO.box, MAT.stone, [.26, 1.45, -.12], [.19, .08, .19]);
  add(house, GEO.box, MAT.wall, [0, .045, .49], [.5, .09, .25]);
  add(house, GEO.box, MAT.wall, [0, .09, .58], [.38, .09, .15]);
  house.scale.setScalar(.88);
  return house;
}
