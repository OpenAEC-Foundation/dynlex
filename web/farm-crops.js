import * as THREE from "./vendor/three/three.module.min.js";

const stalk = new THREE.CylinderGeometry(1, 1, 1, 5);
const root = new THREE.ConeGeometry(1, 1, 9);
const shoulder = new THREE.SphereGeometry(1, 10, 6);
const grain = new THREE.SphereGeometry(1, 5, 4);
const cropMaterial = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: .86, side: THREE.DoubleSide });
const up = new THREE.Vector3(0, 1, 0);
const point = (x, y, z) => new THREE.Vector3(x, y, z);

// Bake the plants in a bed into one colored mesh. Fine leaflets and wheat awns
// can then share one draw call instead of adding hundreds of tiny objects.
function cropGeometry() {
  const positions = [], normals = [], colors = [];
  const color = new THREE.Color();
  function append(geometry, tint, matrix = new THREE.Matrix4()) {
    const transformed = geometry.index ? geometry.toNonIndexed() : geometry.clone();
    transformed.applyMatrix4(matrix);
    positions.push(...transformed.attributes.position.array);
    normals.push(...transformed.attributes.normal.array);
    color.set(tint);
    for (let i = 0; i < transformed.attributes.position.count; i++) colors.push(color.r, color.g, color.b);
    transformed.dispose();
  }
  function shape(geometry, tint, position, scale, rotation = new THREE.Quaternion()) {
    append(geometry, tint, new THREE.Matrix4().compose(position, rotation, scale));
  }
  function stem(from, to, width, tint) {
    const delta = to.clone().sub(from);
    shape(stalk, tint, from.clone().add(to).multiplyScalar(.5), point(width, delta.length(), width),
      new THREE.Quaternion().setFromUnitVectors(up, delta.normalize()));
  }
  function leaf(from, to, width, tint, arch = .025) {
    const delta = to.clone().sub(from);
    const side = point(-delta.z, 0, delta.x).normalize().multiplyScalar(width);
    const vertices = [], indices = [];
    for (let i = 0; i <= 4; i++) {
      const t = i / 4, taper = Math.sin(Math.PI * t);
      const center = from.clone().lerp(to, t); center.y += taper * arch;
      for (const s of [-1, 0, 1]) {
        const vertex = center.clone().addScaledVector(side, s * taper);
        vertex.y -= Math.abs(s) * taper * width * .25;
        vertices.push(...vertex.toArray());
      }
      if (i < 4) for (let j = 0; j < 2; j++) {
        const a = i * 3 + j;
        indices.push(a, a + 3, a + 1, a + 1, a + 3, a + 4);
      }
    }
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute("position", new THREE.Float32BufferAttribute(vertices, 3));
    geometry.setIndex(indices); geometry.computeVertexNormals(); append(geometry, tint); geometry.dispose();
  }
  function finish() {
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
    geometry.setAttribute("normal", new THREE.Float32BufferAttribute(normals, 3));
    geometry.setAttribute("color", new THREE.Float32BufferAttribute(colors, 3));
    const mesh = new THREE.Mesh(geometry, cropMaterial);
    mesh.castShadow = mesh.receiveShadow = true;
    return mesh;
  }
  return { shape, stem, leaf, finish };
}

function carrot(art, x, z, seed, stage) {
  const base = point(x, .025, z);
  if (stage > 0) {
    // Only the carrot shoulder breaks the soil surface.
    art.shape(shoulder, 0xd8812b, point(x, -.025, z), point(.055, .043, .055));
    art.shape(root, 0xd8812b, point(x, -.105, z), point(.049, .16, .049),
      new THREE.Quaternion().setFromAxisAngle(point(1, 0, 0), Math.PI));
  }
  for (let frond = 0; frond < (stage === 0 ? 3 : 6); frond++) {
    const angle = frond * 2.399 + seed * .7;
    const radial = point(Math.cos(angle), 0, Math.sin(angle));
    const across = point(-radial.z, 0, radial.x);
    const reach = .15 + .045 * Math.sin(seed + frond * 2);
    const tip = base.clone().addScaledVector(radial, reach); tip.y = .30 + .075 * Math.cos(frond * 1.7 + seed);
    const tint = [0x3f7835, 0x558d3d, 0x326c37][frond % 3];
    art.stem(base, tip, .0045, 0x648942);
    for (let pair = 0; pair < 5; pair++) {
      const t = .30 + pair * .125;
      const origin = base.clone().lerp(tip, t);
      const length = .075 * (1 - t) + .023;
      for (const side of [-1, 1]) {
        const end = origin.clone().addScaledVector(across, side * length).addScaledVector(radial, .037);
        end.y += .025;
        art.leaf(origin, end, .013 * (1 - t) + .004, tint, .009);
        // Split the larger leaflets for the finely divided carrot silhouette.
        if (pair < 3) for (const fork of [-1, 1]) {
          const branch = origin.clone().lerp(end, .48);
          const tooth = end.clone().addScaledVector(radial, fork * .026);
          art.leaf(branch, tooth, .0055, tint, .005);
        }
      }
    }
    art.leaf(base.clone().lerp(tip, .78), tip.clone().add(point(0, .035, 0)), .012, tint);
  }
}

function wheat(art, x, z, seed, stage) {
  const gold = stage === 2;
  const tint = gold ? 0xc29a41 : 0x69924a;
  const height = .51 + .055 * Math.sin(seed * 2.7);
  const bend = point(Math.sin(seed * 1.8) * .045, 0, Math.cos(seed) * .045);
  const base = point(x, 0, z), neck = base.clone().add(bend); neck.y = height;
  art.stem(base, neck, .008, tint);
  for (let i = 0; i < 3; i++) {
    const angle = seed * 1.9 + i * 2.5;
    const start = base.clone().lerp(neck, .22 + i * .21);
    const end = start.clone().add(point(Math.cos(angle) * .16, .06, Math.sin(angle) * .16));
    art.leaf(start, end, .019, gold ? 0xbca153 : 0x568641, .07);
  }
  if (stage === 0) return;
  const ear = neck.clone().add(point(bend.x, .23, bend.z));
  art.stem(neck, ear, .005, tint);
  for (let row = 0; row < 6; row++) {
    const at = neck.clone().lerp(ear, row / 6);
    const taper = 1 - row * .09;
    for (const side of [-1, 1]) {
      const rotation = new THREE.Quaternion().setFromAxisAngle(point(0, 0, 1), -side * .47);
      const center = at.clone().add(point(side * .017 * taper, .016, 0));
      art.shape(grain, gold ? (side < 0 ? 0xdab65f : 0xe5c578) : 0x97ac58, center,
        point(.018 * taper, .038 * taper, .014 * taper), rotation);
      const awn = center.clone().add(point(side * .041, .09 + .014 * Math.sin(seed + row), .006));
      art.stem(center, awn, .0018, gold ? 0xe5ca83 : 0xa6b769);
    }
  }
}

export const cropStage = growth => growth < .2 ? 0 : growth < .7 ? 1 : 2;

export function createCrop(kind, stage) {
  const art = cropGeometry();
  const columns = kind === "wheat" ? 3 : 2;
  for (let row = 0; row < 3; row++) for (let col = 0; col < columns; col++) {
    const seed = row * columns + col;
    const x = (col - (columns - 1) / 2) * (columns === 3 ? .23 : .39) + Math.sin(seed * 17) * .023;
    const z = (row - 1) * .28 + Math.cos(seed * 11) * .019;
    (kind === "carrots" ? carrot : wheat)(art, x, z, seed, stage);
  }
  const group = new THREE.Group(); group.name = `${kind}-${stage}`;
  group.add(art.finish());
  return group;
}
