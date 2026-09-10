import * as THREE from "./vendor/three/three.module.min.js";
export { createCow, createFarmer, createTree, createHouse } from "./farm-characters.js";

const materials = new Map();
export function material(color, options = {}) {
  const key = `${color}:${JSON.stringify(options)}`;
  if (!materials.has(key)) materials.set(key, new THREE.MeshStandardMaterial({ color, roughness: .82, ...options }));
  return materials.get(key);
}
const box = new THREE.BoxGeometry(1, 1, 1);
const sphere = new THREE.SphereGeometry(1, 10, 7);
const cone = new THREE.ConeGeometry(1, 1, 7);
const cylinder = new THREE.CylinderGeometry(1, 1, 1, 10);
const bucketHandle = new THREE.TorusGeometry(.095, .01, 4, 12, Math.PI);
const binding = new THREE.TorusGeometry(1, .08, 4, 16);

export function piece(group, geometry, color, position, scale, rotation = [0, 0, 0]) {
  const mesh = new THREE.Mesh(geometry, material(color));
  mesh.position.set(...position); mesh.scale.set(...scale); mesh.rotation.set(...rotation);
  mesh.castShadow = true; mesh.receiveShadow = true;
  group.add(mesh);
  return mesh;
}

export function createChicken(seed = 0) {
  const chicken = new THREE.Group();
  piece(chicken, sphere, 0xf3e5c9, [0, .25, 0], [.19, .20, .24]);
  piece(chicken, sphere, 0xe1cdae, [-.15, .24, -.03], [.055, .12, .16]);
  const head = new THREE.Group(); head.position.set(0, .40, .16); chicken.add(head);
  piece(head, sphere, 0xffeed3, [0, 0, 0], [.105, .12, .10]);
  piece(head, cone, 0xdb963b, [0, -.005, .13], [.045, .12, .045], [Math.PI / 2, 0, 0]);
  for (const x of [-.086, .086]) piece(head, sphere, 0x252d28, [x, .025, .045], [.016, .018, .02]);
  for (let i = 0; i < 3; i++) piece(head, sphere, 0xb94435, [0, .115, .04 - i * .045], [.026, .053, .035]);
  piece(head, sphere, 0xc85439, [0, -.10, .065], [.025, .06, .03]);
  chicken.userData.legs = [-.065,.065].map(x => {
    const leg = new THREE.Group();leg.position.set(x,.13,0);
    piece(leg, cylinder, 0xd09a42, [0, -.065, 0], [.012, .13, .012]);
    piece(leg, box, 0xd09a42, [0, -.118, .035], [.045, .025, .10]);
    chicken.add(leg);return leg;
  });
  for (let i = 0; i < 3; i++) piece(chicken, cone, 0xe5d3b8, [(i - 1) * .055, .33, -.22], [.07, .30, .07], [-.7, 0, (i - 1) * .25]);
  chicken.userData.head = head;
  chicken.rotation.y = seed;
  return chicken;
}

export function createFlag(color) {
  const group = new THREE.Group();
  piece(group, cylinder, 0xd6c19a, [0, .67, 0], [.022, 1.34, .022]);
  piece(group, sphere, 0xe9d7a8, [0, 1.38, 0], [.047, .047, .047]);
  const flag = piece(group, box, color, [.22, 1.16, 0], [.43, .27, .022]);
  const stripe = piece(group, box, 0xfaf0d5, [.22, 1.20, .014], [.37, .025, .006]);
  group.userData.cloth = [flag, stripe];
  piece(group, cylinder, 0x756e50, [0, .015, 0], [.15, .03, .15]);
  return group;
}

export function createEggs(count) {
  const group = new THREE.Group();
  for(let i=0;i<Math.min(count,6);i++) piece(group,sphere,0xf2e9d6,
    [(i%3-1)*.13,.09,Math.floor(i/3)*.16],[.055,.085,.06],[.1,0,(i%2-.5)*.4]);
  return group;
}

export function createItem(kind) {
  const group = new THREE.Group();
  group.name = kind;
  if (["axe", "hoe", "shovel"].includes(kind)) {
    group.userData.grip = [0, kind === "shovel" ? .34 : .16, 0];
    piece(group, cylinder, 0x9d703e, [0, .23, 0], [.025, .46, .025]);
    if (kind === "axe") piece(group, box, 0x9dacac, [.07, .40, 0], [.18, .12, .045]);
    if (kind === "hoe") piece(group, box, 0x7e9298, [0, .43, .04], [.18, .04, .11]);
    if (kind === "shovel") piece(group, sphere, 0x839a9b, [0, .03, 0], [.075, .12, .025]);
  } else if (kind === "bucket" || kind === "milk") {
    group.userData.grip = [0, .345, 0];
    piece(group, cylinder, kind === "bucket" ? 0x849da1 : 0xe9e5cc, [0, .13, 0], [.10, .24, .10]);
    piece(group, cylinder, kind === "bucket" ? 0x4b8291 : 0xe8eddf, [0, .255, 0], [.085, .008, .085]);
    const handle = piece(group, bucketHandle, 0x4b666b, [0, .25, 0], [1, 1, 1]);
    handle.rotation.z = 0;
  } else if (kind === "logs") {
    group.userData.grip = [0, .25, 0];
    for (const x of [-.07, .07]) {
      piece(group, cylinder, 0x70513a, [x, .11, 0], [.065, .40, .065], [Math.PI / 2, 0, 0]);
      piece(group, cylinder, 0xc8a276, [x, .11, .203], [.055, .006, .055], [Math.PI / 2, 0, 0]);
    }
    for (const z of [-.11, .11]) piece(group, binding, 0xb99a62, [0, .11, z], [.14, .07, .07]);
    piece(group, bucketHandle, 0xb99a62, [0, .155, 0], [1, 1, 1]);
  } else {
    group.userData.grip = [0, .30, 0];
    piece(group, sphere, 0xcbb58a, [0, .14, 0], [.14, .18, .12]);
    piece(group, cylinder, 0x806749, [0, .30, 0], [.052, .035, .052]);
    const color = { seeds: 0x746b36, carrots: 0xe69236, wheat: 0xe3bb56, hay: 0xb4ae5b, eggs: 0xf2e9d6, scraps: 0x5e7b45, fertilizer: 0x69513c }[kind];
    piece(group, sphere, color, [0, .14, .112], [.065, .075, .018]);
  }
  return group;
}

export function createCompost() {
  const group = new THREE.Group();
  for (const x of [-.4, .4]) for (const z of [-.36, .36]) piece(group, box, 0x786342, [x, .29, z], [.065, .58, .065]);
  for (let row = 0; row < 4; row++) {
    for (const x of [-.4, .4]) piece(group, box, 0x9c8352, [x, .08 + row * .12, 0], [.05, .075, .76]);
    piece(group, box, 0x9c8352, [0, .08 + row * .12, -.36], [.78, .075, .05]);
  }
  const heap = piece(group, sphere, 0x604d35, [0, .10, 0], [.37, .15, .33]);
  group.userData.heap = heap;
  return group;
}

export function createWaterMaterial(time) {
  const water = new THREE.MeshStandardMaterial({ color: 0x38979b, roughness: .22, metalness: .25 });
  water.onBeforeCompile = shader => {
    shader.uniforms.farmTime = time;
    shader.vertexShader = `uniform float farmTime;\n${shader.vertexShader}`.replace("#include <beginnormal_vertex>",
      `#include <beginnormal_vertex>
      vec3 p = (modelMatrix * vec4(position, 1.0)).xyz;
      objectNormal.x += .10 * sin(p.x * 21.0 + farmTime);
      objectNormal.z += .07 * cos(p.z * 18.0 - farmTime);`);
  };
  return water;
}
